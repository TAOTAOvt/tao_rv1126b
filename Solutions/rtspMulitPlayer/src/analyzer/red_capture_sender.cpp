#include "red_capture_sender.h"
#include <opencv2/opencv.hpp>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

static long long g_lastMs[4] = {0, 0, 0, 0};
static const int COOLDOWN_MS = 3000;
static std::string g_serverUrl;

static long long now_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

// ─── Gửi 1 file, trả về true nếu thành công ──────────────────────────────────
static bool send_file(const char *path)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "curl -s --connect-timeout 3 --max-time 10 "  // timeout cứng 10s
        "-o /dev/null -w \"%%{http_code}\" "
        "-X POST %s/upload -F \"image=@%s\"",
        g_serverUrl.c_str(), path);

    FILE *fp = popen(cmd, "r");
    if (!fp) return false;

    char buf[16] = {0};
    fgets(buf, sizeof(buf), fp);
    pclose(fp);

    bool ok = strncmp(buf, "200", 3) == 0;
    printf("[SEND] %s → HTTP %s %s\n", path, buf, ok ? "OK" : "FAIL");
    return ok;
}

// ─── Thread retry + send, chạy ngầm hoàn toàn ────────────────────────────────
static void *retry_thread(void *arg)
{
    while (1) {
        sleep(10);

        DIR *dir = opendir("./red_captures");
        if (!dir) continue;

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;

            char *ext = strrchr(entry->d_name, '.');
            if (!ext || strcmp(ext, ".jpg") != 0) continue;

            char path[512];
            snprintf(path, sizeof(path), "./red_captures/%s", entry->d_name);
            printf("[RETRY] trying: %s\n", path);

            if (send_file(path)) {
                remove(path);
                printf("[DELETE] %s removed\n", path);
            } else {
                printf("[KEEP] %s, retry later\n", path);
                break;  // mạng lỗi → dừng, chờ 10s nữa
            }
        }

        closedir(dir);
    }
    pthread_exit(NULL);
}

// ─── Thread gửi ngay 1 file rồi thoát ────────────────────────────────────────
static void *send_once_thread(void *arg)
{
    char *path = (char *)arg;

    if (send_file(path)) {
        remove(path);
        printf("[DELETE] %s removed\n", path);
    } else {
        printf("[KEEP] %s → retry_thread sẽ lo\n", path);
        // Không xóa → retry_thread tự quét gửi lại sau
    }

    free(path);
    pthread_exit(NULL);
}

static void spawn_send(const char *path)
{
    char *pathCopy = strdup(path);  // thread tự free

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, send_once_thread, pathCopy);
    pthread_attr_destroy(&attr);
}

// ─── Public API ───────────────────────────────────────────────────────────────
void red_capture_init(const char *serverUrl)
{
    mkdir("./red_captures", 0777);
    g_serverUrl = serverUrl ? serverUrl : "";

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, retry_thread, NULL);
    pthread_attr_destroy(&attr);

    printf("[RED_CAPTURE] init done\n");
}

void red_capture_push(int chnId, const cv::Mat &frame)
{
    if (chnId < 0 || chnId >= 4) return;
    if (frame.empty()) return;

    long long now = now_ms();
    if (now - g_lastMs[chnId] < COOLDOWN_MS) return;
    g_lastMs[chnId] = now;

    char path[256];
    snprintf(path, sizeof(path), "./red_captures/ch%d_%lld.jpg", chnId, now);
    cv::imwrite(path, frame);
    printf("[RED_CAPTURE] saved: %s\n", path);

    // Gửi ngay nhưng KHÔNG block — chạy thread riêng
    if (!g_serverUrl.empty()) {
        spawn_send(path);
    }
}

void red_capture_deinit() {}