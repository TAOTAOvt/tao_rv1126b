// zone_overlay.h
#pragma once
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>

typedef enum {
    ZONE_GREEN  = 0,
    ZONE_YELLOW = 1,
    ZONE_RED    = 2,
} ZoneLevel_t;

typedef struct {
    int            id;
    ZoneLevel_t    level;
    std::string    label;
    std::vector<cv::Point> points;  // tọa độ gốc (theo frame gốc camera)
} Zone_t;

typedef struct {
    int               chnId;
    std::vector<Zone_t> zones;

    // Cache — pre-render 1 lần, dùng mãi
    cv::Mat  overlayBGR;   // polygon đã vẽ sẵn, cùng size với cell
    bool     cacheReady = false;
} ChnZones_t;

// API
int  zone_load_config(const char *jsonPath,
                      int cellW, int cellH,
                      float scaleX, float scaleY);
void zone_apply_overlay(cv::Mat &cell, int chnId);

ZoneLevel_t zone_get_level(int chnId, int x, int y);
void zone_free();
