#pragma once
#include <opencv2/opencv.hpp>

void red_capture_init(const char* serverUrl);
void red_capture_push(int chnId, const cv::Mat& frame);
void red_capture_deinit();