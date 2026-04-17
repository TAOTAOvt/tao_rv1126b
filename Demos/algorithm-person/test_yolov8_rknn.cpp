#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

#include <vector>
#include <string>
#include <iostream>
#include <fstream>

#include <opencv2/opencv.hpp>
#include "rknn_api.h"

static unsigned char* load_model(const char* filename, int* model_size)
{
    struct stat st;
    if (stat(filename, &st) < 0) {
        printf("❌ Cannot stat model file: %s\n", filename);
        return nullptr;
    }

    int size = st.st_size;
    unsigned char* data = (unsigned char*)malloc(size);
    if (!data) {
        printf("❌ malloc model buffer failed\n");
        return nullptr;
    }

    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        printf("❌ Cannot open model file: %s\n", filename);
        free(data);
        return nullptr;
    }

    int ret = fread(data, 1, size, fp);
    fclose(fp);

    if (ret != size) {
        printf("❌ fread model failed, ret=%d size=%d\n", ret, size);
        free(data);
        return nullptr;
    }

    *model_size = size;
    return data;
}

static const char* get_tensor_type_string(rknn_tensor_type type)
{
    switch (type) {
        case RKNN_TENSOR_FLOAT32: return "FLOAT32";
        case RKNN_TENSOR_FLOAT16: return "FLOAT16";
        case RKNN_TENSOR_INT8:    return "INT8";
        case RKNN_TENSOR_UINT8:   return "UINT8";
        case RKNN_TENSOR_INT16:   return "INT16";
        case RKNN_TENSOR_UINT16:  return "UINT16";
        case RKNN_TENSOR_INT32:   return "INT32";
        case RKNN_TENSOR_UINT32:  return "UINT32";
        default: return "UNKNOWN";
    }
}

static const char* get_tensor_fmt_string(rknn_tensor_format fmt)
{
    switch (fmt) {
        case RKNN_TENSOR_NCHW: return "NCHW";
        case RKNN_TENSOR_NHWC: return "NHWC";
        default: return "UNKN_FM";
    }
}

static void print_tensor_attr(const char* prefix, rknn_tensor_attr* attr)
{
    printf("%s index=%d name=%s n_dims=%d dims=[",
           prefix,
           attr->index,
           attr->name,
           attr->n_dims);

    for (uint32_t i = 0; i < attr->n_dims; ++i) {
        printf("%d", attr->dims[i]);
        if (i != attr->n_dims - 1) printf(", ");
    }
    printf("] n_elems=%d size=%d fmt=%s type=%s qnt_type=%d zp=%d scale=%f\n",
           attr->n_elems,
           attr->size,
           get_tensor_fmt_string(attr->fmt),
           get_tensor_type_string(attr->type),
           attr->qnt_type,
           attr->zp,
           attr->scale);
}

static void print_first_values_float(const float* data, int count, const char* name)
{
    printf("%s first %d values: ", name, count);
    for (int i = 0; i < count; ++i) {
        printf("%.6f ", data[i]);
    }
    printf("\n");
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        printf("Usage: %s <model.rknn> <image.jpg>\n", argv[0]);
        return -1;
    }

    const char* model_path = argv[1];
    const char* image_path = argv[2];

    // 1) Load model
    int model_size = 0;
    unsigned char* model_data = load_model(model_path, &model_size);
    if (!model_data) {
        return -1;
    }

    rknn_context ctx = 0;
    int ret = rknn_init(&ctx, model_data, model_size, 0, NULL);
    if (ret < 0) {
        printf("❌ rknn_init failed ret=%d\n", ret);
        free(model_data);
        return -1;
    }
    printf("✅ rknn_init success\n");

    // model_data no longer needed after init in most cases
    free(model_data);
    model_data = nullptr;

    // 2) Query SDK / IO num
    rknn_sdk_version version;
    memset(&version, 0, sizeof(version));
    ret = rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(version));
    if (ret == 0) {
        printf("SDK: api=%s, drv=%s\n", version.api_version, version.drv_version);
    }

    rknn_input_output_num io_num;
    memset(&io_num, 0, sizeof(io_num));
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret < 0) {
        printf("❌ rknn_query IO num failed ret=%d\n", ret);
        rknn_destroy(ctx);
        return -1;
    }
    printf("Model inputs=%u, outputs=%u\n", io_num.n_input, io_num.n_output);

    // 3) Query input attrs
    std::vector<rknn_tensor_attr> input_attrs(io_num.n_input);
    for (uint32_t i = 0; i < io_num.n_input; ++i) {
        memset(&input_attrs[i], 0, sizeof(rknn_tensor_attr));
        input_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &input_attrs[i], sizeof(rknn_tensor_attr));
        if (ret < 0) {
            printf("❌ query input attr %u failed ret=%d\n", i, ret);
            rknn_destroy(ctx);
            return -1;
        }
        print_tensor_attr("INPUT", &input_attrs[i]);
    }

    // 4) Query output attrs
    std::vector<rknn_tensor_attr> output_attrs(io_num.n_output);
    for (uint32_t i = 0; i < io_num.n_output; ++i) {
        memset(&output_attrs[i], 0, sizeof(rknn_tensor_attr));
        output_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &output_attrs[i], sizeof(rknn_tensor_attr));
        if (ret < 0) {
            printf("❌ query output attr %u failed ret=%d\n", i, ret);
            rknn_destroy(ctx);
            return -1;
        }
        print_tensor_attr("OUTPUT", &output_attrs[i]);
    }

    // 5) Determine input size
    int input_width = 0;
    int input_height = 0;
    int input_channel = 3;

    // Usually dims are [N,C,H,W] or [N,H,W,C]
    if (input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
        input_channel = input_attrs[0].dims[1];
        input_height  = input_attrs[0].dims[2];
        input_width   = input_attrs[0].dims[3];
    } else {
        input_height  = input_attrs[0].dims[1];
        input_width   = input_attrs[0].dims[2];
        input_channel = input_attrs[0].dims[3];
    }

    printf("Input shape parsed: W=%d H=%d C=%d\n", input_width, input_height, input_channel);

    // 6) Read image
    cv::Mat bgr = cv::imread(image_path);
    if (bgr.empty()) {
        printf("❌ Failed to read image: %s\n", image_path);
        rknn_destroy(ctx);
        return -1;
    }

    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);

    cv::Mat resized;
    cv::resize(rgb, resized, cv::Size(input_width, input_height));

    if (!resized.isContinuous()) {
        resized = resized.clone();
    }

    // 7) Set input
    rknn_input input;
    memset(&input, 0, sizeof(input));
    input.index = 0;
    input.buf = resized.data;
    input.size = input_width * input_height * input_channel;
    input.pass_through = 0;
    input.type = RKNN_TENSOR_UINT8;
    input.fmt = RKNN_TENSOR_NHWC;  // feed image as HWC

    ret = rknn_inputs_set(ctx, 1, &input);
    if (ret < 0) {
        printf("❌ rknn_inputs_set failed ret=%d\n", ret);
        rknn_destroy(ctx);
        return -1;
    }

    // 8) Run
    ret = rknn_run(ctx, NULL);
    if (ret < 0) {
        printf("❌ rknn_run failed ret=%d\n", ret);
        rknn_destroy(ctx);
        return -1;
    }
    printf("✅ rknn_run success\n");

    // 9) Get outputs
    std::vector<rknn_output> outputs(io_num.n_output);
    for (uint32_t i = 0; i < io_num.n_output; ++i) {
        memset(&outputs[i], 0, sizeof(rknn_output));
        outputs[i].want_float = 1;  // convenient for debug
        outputs[i].is_prealloc = 0;
    }

    ret = rknn_outputs_get(ctx, io_num.n_output, outputs.data(), NULL);
    if (ret < 0) {
        printf("❌ rknn_outputs_get failed ret=%d\n", ret);
        rknn_destroy(ctx);
        return -1;
    }

    printf("✅ rknn_outputs_get success\n");

    // 10) Dump first few output values
    for (uint32_t i = 0; i < io_num.n_output; ++i) {
        printf("\n---- OUTPUT %u ----\n", i);
        print_tensor_attr("OUT_ATTR", &output_attrs[i]);

        if (!outputs[i].buf) {
            printf("output[%u].buf is null\n", i);
            continue;
        }

        int show_count = output_attrs[i].n_elems < 10 ? output_attrs[i].n_elems : 10;
        float* out_ptr = (float*)outputs[i].buf;
        print_first_values_float(out_ptr, show_count, "data");
    }

    // 11) release outputs
    rknn_outputs_release(ctx, io_num.n_output, outputs.data());

    // 12) destroy
    rknn_destroy(ctx);
    printf("✅ Done\n");
    return 0;
}
