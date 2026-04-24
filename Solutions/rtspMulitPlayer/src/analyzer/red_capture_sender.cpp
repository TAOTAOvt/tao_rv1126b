#include "red_capture_sender.h"
#include <opencv2/opencv.hpp>
#include <sys/stat.h>
#include <time.h>
#include <stdio.h>

static long long g_lastMs[4] = {0,0,0,0};
static const int COOLDOWN_MS = 3000;

static long long now_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

void red_capture_init(const char* serverUrl)
{
    mkdir("./red_captures", 0777);
}

void red_capture_push(int chnId, const cv::Mat& frame)
{
    if (chnId < 0 || chnId >= 4) return;
    if (frame.empty()) return;

    long long now = now_ms();
    if (now - g_lastMs[chnId] < COOLDOWN_MS) return;
    g_lastMs[chnId] = now;

    char path[256];
    snprintf(path, sizeof(path), "./red_captures/ch%d_%lld.jpg", chnId, now);

    cv::imwrite(path, frame);
    printf("[RED_CAPTURE] saved %s\n", path);
}

void red_capture_deinit()
{
}