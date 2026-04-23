
//=====================  C++  =====================
#include <string>
#include <list>
//=====================   C   =====================
#include "system.h"
//=====================  PRJ  =====================
#include "system_opt.h"
#include "log_manager.h"
#include "rga_wrapper.h"
#include "display.h"
#include "../zone_overlay.h"
#include "analyzer.h"

#include <fcntl.h>   // open
#include <unistd.h>  // write, close

static volatile bool g_redDetected = false;
static volatile bool g_redForDisplay[4]    = {false, false, false, false};
static volatile bool g_yellowForDisplay[4] = {false, false, false, false};  // THÊM
static Scalar colorArray[4] = {
    Scalar(0, 180, 0),
    Scalar(0, 220, 220),
    Scalar(0, 0, 255),
    Scalar(128, 128, 128)
};

using namespace cv;

enum BoxColor : unsigned char {
    BOX_GREEN  = 0,
    BOX_YELLOW = 1,
    BOX_RED    = 2,
    BOX_GRAY   = 3
};

static void gpio_write(const char *val)
{
    int fd = open("/sys/class/gpio/gpio178/value", O_WRONLY);
    if (fd >= 0) { write(fd, val, 1); close(fd); }
}

static void *buzzer_thread(void *para)
{
    bool buzzerOn = false;
    int  noPersonCount = 0;
    const int OFF_DELAY = 1;  // 4 x 500ms = 2s mới tắt

    while (1) {
        bool detected = g_redDetected;
        g_redDetected = false;  // reset, display set lại nếu vẫn có người

        if (detected) {
            noPersonCount = 0;
            buzzerOn = true;
        } else {
            if (buzzerOn) {
                noPersonCount++;
                if (noPersonCount >= OFF_DELAY) {
                    buzzerOn = false;
                    noPersonCount = 0;
                    gpio_write("0");
                }
            }
        }

        if (buzzerOn) {
            gpio_write("1");
            usleep(500000);  // bật 500ms
            gpio_write("0");
            usleep(500000);  // tắt 500ms
        } else {
            usleep(500000);  // nghỉ khi không active
        }
    }

    gpio_write("0");
    pthread_exit(NULL);
}

static void buzzer_init()
{
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, buzzer_thread, NULL);
    pthread_attr_destroy(&attr);
}

static unsigned char pick_box_color_by_zone(int chn, int x1, int y1, int x2, int y2)
{
    // Lấy 5 điểm kiểm tra: 4 góc + tâm
    cv::Point pts[5] = {
        cv::Point(x1, y1),
        cv::Point(x2, y1),
        cv::Point(x1, y2),
        cv::Point(x2, y2),
        cv::Point((x1 + x2) / 2, (y1 + y2) / 2)
    };

    bool hitRed = false;
    bool hitYellow = false;
    bool hitGreen = false;

    for (int i = 0; i < 5; ++i) {
        ZoneLevel_t lvl = zone_get_level(chn, pts[i].x, pts[i].y);  
        // hàm này cần có trong zone_overlay / zones

        if (lvl == ZONE_RED) {
            hitRed = true;
            break;
        } else if (lvl == ZONE_YELLOW) {
            hitYellow = true;
        } else if (lvl == ZONE_GREEN) {
            hitGreen = true;
        }
    }

    if (hitRed)    return BOX_RED;
    if (hitYellow) return BOX_YELLOW;
    if (hitGreen)  return BOX_GREEN;
    return BOX_GRAY;
}
static int plot_one_box(Mat src, int x1, int x2, int y1, int y2, char *label, char colour)
{
    int tl = round(0.002 * (src.rows + src.cols) / 2) + 1;
    rectangle(src, cv::Point(x1, y1), cv::Point(x2, y2), colorArray[(unsigned char)colour], 3);

    int tf = max(tl -1, 1);

    int base_line = 0;
    cv::Size t_size = getTextSize(label, FONT_HERSHEY_SIMPLEX, (float)tl/3, tf, &base_line);
    int x3 = x1 + t_size.width;
    int y3 = y1 - t_size.height - 3;

    rectangle(src, cv::Point(x1, y1), cv::Point(x3, y3), colorArray[(unsigned char)colour], -1);
    putText(src, label, cv::Point(x1, y1 - 2), FONT_HERSHEY_SIMPLEX, (float)tl/3, cv::Scalar(255, 255, 255, 255), tf, 8);
    return 0;
}
static void paint_algorithm_result_scaled(Mat image, int chnId, ChnResult_t result, float scaleX, float scaleY)
{
    char text[256];
    bool redZoneHit = false;  // <-- thêm
    bool yellowZoneHit = false;  // THÊM

    for (int algoIndex = 0; algoIndex < ALGOMAXNUM; algoIndex++) {
        for (int j = 0; j < result.algoRes[algoIndex].resNumber; j++) {
            detect_result_t *det = &(result.algoRes[algoIndex].detect_Group.results[j]);
            if (det->prop < 0.3)
                continue;

            int x1 = (int)(det->box.left   * scaleX);
            int y1 = (int)(det->box.top    * scaleY);
            int x2 = (int)(det->box.right  * scaleX);
            int y2 = (int)(det->box.bottom * scaleY);

            x1 = std::max(0, std::min(x1, image.cols - 1));
            y1 = std::max(0, std::min(y1, image.rows - 1));
            x2 = std::max(0, std::min(x2, image.cols - 1));
            y2 = std::max(0, std::min(y2, image.rows - 1));

            sprintf(text, "%s %.1f%%", det->name, det->prop * 100);
            unsigned char boxColor = pick_box_color_by_zone(chnId, x1, y1, x2, y2);
            plot_one_box(image, x1, x2, y1, y2, text, boxColor);

            // Nếu box màu đỏ → có người trong vùng đỏ
            if (boxColor == BOX_RED)
                redZoneHit = true;  // <-- thêm
            if (boxColor == BOX_YELLOW) 
                yellowZoneHit = true;  // THÊM
        }
    }
    // Điều khiển còi
    if (redZoneHit)
    {
        g_redDetected = true;
        g_redForDisplay[chnId] = true;       // THÊM MỚI cho display
    }

    if (yellowZoneHit)
    {
        g_yellowForDisplay[chnId] = true;         // THÊM
    }                                             //

}

class Analyzer
{
public:
	Analyzer(int32_t maxChn);
	~Analyzer();

    static Analyzer *instance() { return m_pSelf; }
    static void createAnalyzer(int32_t maxChn);
    
    // --视频资源处理
    // 1.更新某路[视频]通道图像数据
    int32_t upDateVideoChannel(int chnId, char *imgData, ImgDesc_t imgDesc);
    // 2.取某路[视频]通道图像数据地址
    vChnObject *getVideoChnObject(int chnId);
    uint8_t* videoChannelData(vChnObject *pVideoObj, int &width, int &height);
    // 3.取某路[视频]通道的分析结果
    int32_t videoChannelAnalyRes(int chnId);
    

    bool mAnalyzeThreadWorking;
    bool mDisplayThreadWorking;
    pthread_mutex_t mVideoChnLock;
    //pthread_mutex_t mAudioChnLock;
    int32_t mMaxChnNum;
protected:
    vChnObject *createVideoChnObject(int32_t chnId, int32_t imgWidth, int32_t imgHeight);
    int32_t releaseVideoChnObject(vChnObject *pObj);
    int32_t delAllVideoChannel();

    //aChnObject *searchAudioChnObject(int chnId);
    //aChnObject *createAudioChnObject();
    //int32_t releaseAudioChnObject(aChnObject *pObj);
    //int32_t delAllAudioChannel();

    
private:
    static Analyzer *m_pSelf;
    
    // 解码器输出数据 - RGB格式
	std::list<vChnObject*> m_VideoChannellist;
	//std::list<aChnObject*> m_MediaAudioChannellist;

	pthread_t mAnalyzeTid;
	pthread_t mDisplayTid;
};


static void *imgAnalyze_thread(void *para)
{
    Analyzer *pSelf = (Analyzer *)para;

    int chnId = 0;
    Mat image;
    ChnResult_t result;
    pSelf->mAnalyzeThreadWorking = true;
    while(1){
        if(!pSelf->mAnalyzeThreadWorking){
            msleep(5);
            break;
        }
        
        if(NULL == pSelf){
            msleep(5);
            break;
        }
        
        vChnObject *pVideoObj = pSelf->getVideoChnObject(chnId);
        if(pVideoObj){
            // 取出待分析图像
            pthread_rwlock_rdlock(&pVideoObj->imgLock);
            pVideoObj->image.copyTo(image);
            pthread_rwlock_unlock(&pVideoObj->imgLock);

            // 此步骤操作会比较耗时，因此在给pVideoObj->chnResult赋值时需要重新判断pVideoObj是否存在
            result = algorithm_process(chnId, image);
        }        
        pVideoObj = pSelf->getVideoChnObject(chnId);
        if(pVideoObj){
            // 其实这里还是有可能会在切(不同分辨率)流时，会导致应用崩溃
            memcpy(&pVideoObj->chnResult, &result, sizeof(ChnResult_t));
        }
        
        chnId++;
        chnId%=pSelf->mMaxChnNum;
        msleep(20);
    }
    
    pthread_exit(NULL);
}

static void *imgDisplay_thread(void *para)
{
    Analyzer *pSelf = (Analyzer *)para;

    int screenW, screenH;
    int flashTick = 0;
    disp_init(&screenW, &screenH);
    printf("disp_init: screenW=%d screenH=%d\n", screenW, screenH);

    const int outW = 1280;
    const int outH = 800;
    const int cellW = outW / 2;
    const int cellH = outH / 2;

    float zoneScaleX = (float)cellW / 1280.0f;
    float zoneScaleY = (float)cellH / 720.0f;
    // zone_load_config("./zones.json",
    //                 cellW, cellH,
    //                 zoneScaleX, zoneScaleY);
    int zret = zone_load_config("./src/zones.json", cellW, cellH, zoneScaleX, zoneScaleY);
    printf("[ZONE] load ret=%d\n", zret);

    cv::Mat canvas(outH, outW, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Mat noSignal_img = cv::imread("./noSignal.jpg", 1);
    cv::Mat noSignal_cell;

    if (!noSignal_img.empty()) {
        cv::resize(noSignal_img, noSignal_cell, cv::Size(cellW, cellH));
    } else {
        noSignal_cell = cv::Mat(cellH, cellW, CV_8UC3, cv::Scalar(0, 0, 0));
    }

    pSelf->mDisplayThreadWorking = true;

    while (1) {
        if (!pSelf->mDisplayThreadWorking) {
            msleep(5);
            break;
        }

        for (int i = 0; i < pSelf->mMaxChnNum && i < 4; i++) {
        int x = (i % 2) * cellW;
        int y = (i / 2) * cellH;
        cv::Mat cell = canvas(cv::Rect(x, y, cellW, cellH));

        vChnObject *pVideoObj = pSelf->getVideoChnObject(i);
        bool drawn = false;

        if (pVideoObj) {
            pthread_rwlock_rdlock(&pVideoObj->imgLock);

            if (!pVideoObj->image.empty()) {
                // Lưu size gốc trước khi resize (để tính scale)
                float scaleX = (float)cellW / pVideoObj->image.cols;
                float scaleY = (float)cellH / pVideoObj->image.rows;

                if (pVideoObj->image.cols == cellW && pVideoObj->image.rows == cellH) {
                    pVideoObj->image.copyTo(cell);
                } else {
                    cv::resize(pVideoObj->image, cell, cv::Size(cellW, cellH), 0, 0, cv::INTER_LINEAR);
                }

                // Copy result trong lock
                ChnResult_t result;
                memcpy(&result, &pVideoObj->chnResult, sizeof(ChnResult_t));

                pthread_rwlock_unlock(&pVideoObj->imgLock);

                // Vẽ BBox sau khi unlock (không block capture thread)
                zone_apply_overlay(cell, i);
                paint_algorithm_result_scaled(cell, i, result, scaleX, scaleY);

                // char txt[16];
                // snprintf(txt, sizeof(txt), "CH%d", i);
                // cv::putText(cell, txt, cv::Point(20, 40),
                //             cv::FONT_HERSHEY_SIMPLEX, 1.0,
                //             cv::Scalar(0, 255, 255), 2);

                const char *chLabels[4] = {"Foward", "Backward", "Left", "Right"};
                cv::putText(cell, chLabels[i], cv::Point(20, 40),
                            cv::FONT_HERSHEY_SIMPLEX, 1.0,
                            cv::Scalar(0, 255, 255), 2);
                drawn = true;
            } else {
                pthread_rwlock_unlock(&pVideoObj->imgLock);
            }
        }
            if (!drawn) {
                noSignal_cell.copyTo(cell);
            }
        }

        flashTick++;

        for (int i = 0; i < 4; i++) {
            bool hasRed    = g_redForDisplay[i];
            bool hasYellow = g_yellowForDisplay[i];

            if (!hasRed && !hasYellow) continue;

            g_redForDisplay[i]    = false;
            g_yellowForDisplay[i] = false;

            // Blink: 8 tick sáng / 8 tick tắt (~30fps → ~4 lần/giây)
            if ((flashTick / 8) % 2 == 0) continue;  // tick tắt → bỏ qua, không vẽ overlay

            int fx = (i % 2) * cellW;
            int fy = (i / 2) * cellH;
            cv::Mat cell = canvas(cv::Rect(fx, fy, cellW, cellH));

            if (hasRed) {
                cv::Mat ov(cell.size(), CV_8UC3, cv::Scalar(0, 0, 160));
                cv::addWeighted(cell, 0.72, ov, 0.28, 0, cell);
                cv::rectangle(cell, cv::Point(3, 3),
                            cv::Point(cellW - 4, cellH - 4),
                            cv::Scalar(0, 0, 255), 5);
            } else {
                cv::Mat ov(cell.size(), CV_8UC3, cv::Scalar(0, 160, 220));
                cv::addWeighted(cell, 0.78, ov, 0.22, 0, cell);
                cv::rectangle(cell, cv::Point(3, 3),
                            cv::Point(cellW - 4, cellH - 4),
                            cv::Scalar(0, 200, 255), 4);
            }
        }
        disp_commit(canvas.data, canvas.cols, canvas.rows, HAL_TRANSFORM_ROT_270);

        // 15ms ~ 66fps -> quá dày
        msleep(33);   // ~30fps
        // cần giảm tiếp thì msleep(50); // ~20fps
    }

    disp_exit();
    pthread_exit(NULL);
}

Analyzer *Analyzer::m_pSelf = NULL;
Analyzer::Analyzer(int32_t maxChn) :
    mAnalyzeThreadWorking(false),
    mDisplayThreadWorking(false),
    mMaxChnNum(maxChn)
{
    rga_init();
    
    /*初始化通道锁*/
    pthread_mutex_init(&mVideoChnLock, NULL);
    //pthread_mutex_init(&mAudioChnLock, NULL);
    
    /*创建线程*/
    if(0 != CreateJoinThread(imgAnalyze_thread, this, &mAnalyzeTid)){
        return ;
    }
    
    if(0 != CreateJoinThread(imgDisplay_thread, this, &mDisplayTid)){
        return ;
    }
}
Analyzer::~Analyzer()
{
    /*回收线程*/
    // 1，等待取流线程跑起来
    int timeOut_ms = 1000; //设置n(ms)超时，超时就不等了
    while(1){
        if(((true == mDisplayThreadWorking)&&(true == mAnalyzeThreadWorking))||(timeOut_ms <= 0)){
            break;
        }
        timeOut_ms--;
        usleep(1000);
    }
    // 2，退出线程并等待其结束
    mAnalyzeThreadWorking = false;
    // --[等待分析线程结束]--
    while(1) {
        usleep(20*1000);
        int32_t exitCode = pthread_join(mAnalyzeTid, NULL);
        if(0 == exitCode){
            break;
        }else if(0 != exitCode){
            switch (exitCode) {
                case ESRCH:  // 没有找到线程ID
                    PRINT_ERROR("imgAnalyze_thread exit: No thread with the given ID was found.");
                    break;
                case EINVAL: // 线程不可连接或已经有其他线程在等待它
                    PRINT_ERROR("imgAnalyze_thread exit: Thread is detached or already being waited on.");
                    break;
                case EDEADLK: // 死锁 - 线程尝试join自己
                    PRINT_ERROR("imgAnalyze_thread exit: Deadlock detected - thread is trying to join itself.");
                    break;
            }
            continue;
        }
    }
    mDisplayThreadWorking = false;
    // --[等待显示线程结束]--
    while(1) {
        usleep(20*1000);
        int32_t exitCode = pthread_join(mDisplayTid, NULL);
        if(0 == exitCode){
            break;
        }else if(0 != exitCode){
            switch (exitCode) {
                case ESRCH:  // 没有找到线程ID
                    PRINT_ERROR("imgDisplay_thread exit: No thread with the given ID was found.");
                    break;
                case EINVAL: // 线程不可连接或已经有其他线程在等待它
                    PRINT_ERROR("imgDisplay_thread exit: Thread is detached or already being waited on.");
                    break;
                case EDEADLK: // 死锁 - 线程尝试join自己
                    PRINT_ERROR("imgDisplay_thread exit: Deadlock detected - thread is trying to join itself.");
                    break;
            }
            continue;
        }
    }

    /*回收视频资源*/
    delAllVideoChannel();
    pthread_mutex_destroy(&mVideoChnLock);

    /*回收音频资源*/
    //delAllAudioChannel();
    //pthread_mutex_destroy(&mAudioChnLock);
    
    rga_unInit();
}

void Analyzer::createAnalyzer(int32_t maxChn)
{
    if(m_pSelf == NULL) {
        m_pSelf = new Analyzer(maxChn);
   }
}

int32_t Analyzer::upDateVideoChannel(int chnId, char *imgData, ImgDesc_t imgDesc)
{
    if(chnId < 0)
        return -1;

    pthread_mutex_lock(&mVideoChnLock);
    vChnObject* targetObj = nullptr;
    for (auto it = m_VideoChannellist.begin(); it != m_VideoChannellist.end(); ++it) {
        // 找到目标对象
        if ((*it)->chnId == chnId) {
            targetObj = *it;  
            
            // 图像信息改变，销毁原来图像缓存
            if((targetObj->image.cols != imgDesc.width)||(targetObj->image.rows != imgDesc.height)){
                if(0 == releaseVideoChnObject(targetObj)){
                    // 从链表中移除chnObj
                    it = m_VideoChannellist.erase(it);
                }else{
                    pthread_mutex_unlock(&mVideoChnLock);
                    return -2;
                }
            }
            
            break;
        }
    }
    
    // 需要创建一个[视频]通道对象
    if (!targetObj) {
        targetObj = createVideoChnObject(chnId, imgDesc.width, imgDesc.height);
        if(!targetObj)
            return -3;
        
        m_VideoChannellist.push_back(targetObj);
    }
    pthread_mutex_unlock(&mVideoChnLock);

    // 更新[视频]通道图像数据
    Image srcImage, dstImage;
    memset(&srcImage, 0, sizeof(srcImage));
    memset(&dstImage, 0, sizeof(dstImage));
    
    srcImage.fmt = rgaFmt(imgDesc.fmt);
    srcImage.width = imgDesc.width;
    srcImage.height = imgDesc.height;
    srcImage.hor_stride = imgDesc.horStride;
    srcImage.ver_stride = imgDesc.verStride;
    srcImage.rotation = HAL_TRANSFORM_ROT_0;
    srcImage.pBuf = imgData;
    
    dstImage.fmt = RK_FORMAT_BGR_888;
    dstImage.width = targetObj->image.cols;
    dstImage.height = targetObj->image.rows;
    dstImage.hor_stride = targetObj->image.cols;
    dstImage.ver_stride = targetObj->image.rows;
    dstImage.rotation = HAL_TRANSFORM_ROT_0;
    dstImage.pBuf = (void *)targetObj->image.data;
    
    pthread_rwlock_wrlock(&targetObj->imgLock);
    srcImg_ConvertTo_dstImg(&dstImage, &srcImage);
    pthread_rwlock_unlock(&targetObj->imgLock);
    return 0;
}

vChnObject *Analyzer::getVideoChnObject(int chnId)
{
    if(chnId < 0)
        return NULL;

    vChnObject* targetObj = nullptr;
    pthread_mutex_lock(&mVideoChnLock);
    for (auto it = m_VideoChannellist.begin(); it != m_VideoChannellist.end(); ++it) {
        // 找到目标对象
        if ((*it)->chnId == chnId) {
            targetObj = *it;
            break;
        }
    }
    pthread_mutex_unlock(&mVideoChnLock);

    return targetObj;
}


vChnObject *Analyzer::createVideoChnObject(int32_t chnId, int32_t imgWidth, int32_t imgHeight)
{
    // 1. 创建通道对象
    vChnObject* newChnObj = new vChnObject;
    if(!newChnObj)
        return NULL;
    
    // 2. 初始化图像数据读写锁
    pthread_rwlock_init(&newChnObj->imgLock, nullptr);
    
    // 3. 创建图像缓存
    newChnObj->chnId = chnId;
    newChnObj->image = Mat(imgHeight, imgWidth, CV_8UC3, Scalar(0, 255, 0));
    memset(&newChnObj->chnResult, 0, sizeof(ChnResult_t));
    
    return newChnObj;
}


int32_t Analyzer::releaseVideoChnObject(vChnObject *pObj)
{
    if(NULL == pObj)
        return -1;
    
    // 1. 销毁Mat资源（OpenCV会自动管理）
    pthread_rwlock_wrlock(&pObj->imgLock);
    pObj->image.release();
    pthread_rwlock_unlock(&pObj->imgLock);
    
    // 2. 销毁读写锁
    pthread_rwlock_destroy(&pObj->imgLock);
    
    // 3. 销毁通道对象
    delete pObj;
    
    return 0;
}

int32_t Analyzer::delAllVideoChannel()
{
    pthread_mutex_lock(&mVideoChnLock);
    for (auto it = m_VideoChannellist.begin(); it != m_VideoChannellist.end(); ++it) {
        if(0 == releaseVideoChnObject(*it)){
            it = m_VideoChannellist.erase(it);
        }
    }
    pthread_mutex_unlock(&mVideoChnLock);
    return 0;
}


int analyzer_init(int32_t maxChn)
{
    // 创建图像分析器
    Analyzer::createAnalyzer(maxChn);
    
    // 模型初始化
    algorithm_init();
    buzzer_init();  // <-- thêm dòng này

    return 0;
}

int videoOutHandle(char *imgData, ImgDesc_t imgDesc)
{
    Analyzer *pAnalyzer = Analyzer::instance();

    if(pAnalyzer){
        pAnalyzer->upDateVideoChannel(imgDesc.chnId, imgData, imgDesc);
    }
    
    return 0;
}

