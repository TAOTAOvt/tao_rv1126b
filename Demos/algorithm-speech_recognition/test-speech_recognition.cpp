#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "sndfile.h"
#include "speech_recognition.h"
#include "audio_utils.h"


int main(int argc, char **argv)
{
	if (argc != 7){
		printf("%s <encoder_path> <decoder_path> <filter_path> <vocab_path> <task(en/cn)> <audio_path>\n", argv[0]);
		printf("Example: %s speech_encoder.model speech_decoder.model filters.txt CN.txt cn 1-10-1_CN.wav\n", argv[0]);
		return -1;
	}
	const char *p_encoder_path = argv[1];	// 编码模型地址
	const char *p_decoder_path = argv[2];	// 解码模型地址
	const char *p_filter_path = argv[3];	// 滤波器频谱
	const char *p_vocab_path = argv[4];		// 词组文件
	const char *p_task = argv[5];			// 识别语种（cn/en）
	const char *p_audio_path = argv[6];		// 待识别音频
	int task_code = 0;
	std::vector<std::string> recognized_text;

	// Tokenizer 预定义控制符号(切换语言或任务)
	if (strcmp(p_task, "en") == 0){
		task_code = 50259;
	}
	else if (strcmp(p_task, "cn") == 0){
		task_code = 50260;
	}
	else{
		printf("\n\033[1;33mCurrently only English or Chinese recognition tasks are supported. Please specify <task> as en or zh\033[0m\n");
		return -1;
	}
	
	// 读取音频，并对音频进行处理
	audio_buffer_t audio;
	int ret = read_audio(p_audio_path, &audio);
	if (ret != 0){
		printf("read audio fail! ret=%d audio_path=%s\n", ret, p_audio_path);
		return -1;
	}
	if (audio.num_channels == 2){
		ret = convert_channels(&audio);
	}
	if (audio.sample_rate != SAMPLE_RATE){
		ret = resample_audio(&audio, audio.sample_rate, SAMPLE_RATE);
	}

	// speech recognition初始化
	rknn_whisper_t whisper;
	ret = speech_recognition_init(p_encoder_path, p_decoder_path, p_filter_path, p_vocab_path, &whisper);

	int iter = 0;
	for (int i=0; i < 5; i++)
	{
		clock_t start = clock();

		recognized_text.clear();
		// speech recognition语音识别
		ret = speech_recognition_run(&whisper, audio, task_code, recognized_text);
		
		clock_t end = clock();  // 记录结束时间
		double infer_time = ((double)(end - start)) / CLOCKS_PER_SEC;  // 转换为秒

		// 结果输出
		std::cout << "\nspeech recognition output: ";
		for (const auto &str : recognized_text){
			std::cout << str;
		}
		std::cout << std::endl;

		float audio_length = audio.num_frames / (float)SAMPLE_RATE; // sec
		audio_length = audio_length > (float)CHUNK_LENGTH ? (float)CHUNK_LENGTH : audio_length;
		float rtf = infer_time / audio_length;
		printf("%d, Real Time Factor (RTF): %.3f / %.3f = %.3f\n", iter++, infer_time, audio_length, rtf);
	}

	// speech recognition释放 
	speech_recognition_release(&whisper);
	return 0;
}
