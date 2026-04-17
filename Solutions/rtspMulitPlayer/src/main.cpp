//=====================  C++  =====================
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
//======================  C  ======================
#include <unistd.h>
//=====================  PRJ  =====================
#include "capturer/capturer.h"
#include "analyzer/analyzer.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
static const SrcCfg_t SrcCfg_tab[] = {
    {
        .srcType = "rtsp",
        .loaction = "rtsp://127.0.0.1:8554/test0",
        .videoEncType = "h264",
        .audioEncType = "null",
    }, 
    {
        .srcType = "rtsp",
        .loaction = "rtsp://127.0.0.1:8554/test1",
        .videoEncType = "h264",
        .audioEncType = "null",
    }, 
    {
        .srcType = "rtsp",
        .loaction = "rtsp://127.0.0.1:8554/test2",
        .videoEncType = "h264",
        .audioEncType = "null",
    },
    {
        .srcType = "rtsp",
        .loaction = "rtsp://127.0.0.1:8554/test3",
        .videoEncType = "h264",
        .audioEncType = "null",
    }
};

int main(int argc, char **argv)
{
    int ret = -1;
    int chnNums = ARRAY_SIZE(SrcCfg_tab);
    if(chnNums <= 0){
        return -1;
    }
    
    /* Initialize algotithm model */
    ret = analyzer_init(chnNums);
    if(0 != ret){
        printf("Initialize algotithm model faild ! ret = %d\n", ret);
        return ret;
    }

    /* Create Display */
#if 0
    Display_t dispDesc = {"rtspMulitPlayer",0,0,1920,1080};
    char **pDispBuffer = dispBufferMap(&dispDesc);
    if(!(*pDispBuffer)){
        return -2;
    }
#endif

    /* Initialize GStreamer */
    gst_init(&argc, &argv);
    
    Capturer *pCapturer[32] = {NULL};
    for(int i = 0; i <chnNums; i++) {
        pCapturer[i] = new Capturer(i, SrcCfg_tab[i]);
        if(pCapturer[i]){
            if(0 != pCapturer[i]->init()){
                printf("playChn[%d] init faild\n", i);
                delete pCapturer[i];
                pCapturer[i] = NULL;
            }
        }
    }

#if 0
    //进入显示事件循环
    display(&dispDesc);
#else
    while(1){
        sleep(2);
    }
#endif

    for(int i = 0; i <chnNums; i++) {
        if(pCapturer[i]){
            delete pCapturer[i];
            pCapturer[i] = NULL;
        }
    }
    
    return 0;
}

