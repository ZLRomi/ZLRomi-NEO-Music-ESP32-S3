#include "music_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "player.h"
#include "settings.h"
#include "ui.h"

static const char *TAG = "music";

EXT_RAM_BSS_ATTR music_state_t g_music;

/* song_list_t 有 8.6KB，放栈上会直接把主任务栈撑爆（栈只有 12KB）。
   临时缓冲统一放这里，三处复用。 */
static EXT_RAM_BSS_ATTR song_list_t s_tmp;

/* ── 歌词后台任务 ────────────────────────────────────────────
 *
 * 歌词是 HTTPS 请求，TLS 握手要吃掉好几 KB 栈。UI 主任务只有 12KB 栈，
 * 在那里做握手会栈溢出 → panic 重启（现象就是「点一下歌卡住然后回到首页」）。
 * 所以歌词一律放到独立任务里取，取完再通知界面刷新。
 *
 * 播放地址则由 player 的音频任务自己取，不经过这里。
 */
/* 12KB：跑 HTTPS + TLS，还要调 NVS / 读 flash。
   ⚠️ 栈必须在内部 RAM：任务栈在 PSRAM 时调用 SPI flash API 会触发
      esp_task_stack_is_sane_cache_disabled() 断言直接崩溃 ——
      这正是「一点歌就闪屏重启」的根因（NVS 就在 flash 上）。 */
#define LYRIC_STACK  (12 * 1024)

typedef struct {
    int64_t id;
} lyric_req_t;

static QueueHandle_t s_lyric_q;
static volatile bool  s_lyric_ready;
/* 歌词缓冲区由后台任务写、UI 任务读，用互斥锁防止读到半截内容 */
static SemaphoreHandle_t s_lyric_lock;

static void lyric_task(void *arg)
{
    (void)arg;
    lyric_req_t req;
    /* 结果缓冲放 PSRAM：4KB 歌词 + 栈，不能再挤内部 RAM */
    static EXT_RAM_BSS_ATTR char lrc_buf[LYRIC_MAX];

    for (;;) {
        if (xQueueReceive(s_lyric_q, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        ESP_LOGI(TAG, "后台取歌词 id=%lld", (long long)req.id);
        bool ok = netease_lyric(req.id, lrc_buf, sizeof(lrc_buf));

        xSemaphoreTake(s_lyric_lock, portMAX_DELAY);
        if (ok) {
            strncpy(g_music.lyric, lrc_buf, sizeof(g_music.lyric) - 1);
            g_music.lyric[sizeof(g_music.lyric) - 1] = '\0';
            g_music.lyric_ok = true;
        } else {
            g_music.lyric[0] = '\0';
            g_music.lyric_ok = false;
        }
        xSemaphoreGive(s_lyric_lock);

        if (ok) {
            ESP_LOGI(TAG, "歌词就绪 id=%lld", (long long)req.id);
        } else {
            ESP_LOGW(TAG, "歌词没取到 id=%lld", (long long)req.id);
        }
        /* 无论成功失败都要置位：否则播放页会一直停在「歌词加载中」 */
        s_lyric_ready = true;
        /* 通知界面重绘，歌词才会显示出来 */
        ui_redraw();
    }
}

static void lyric_request(int64_t id)
{
    if (!s_lyric_q) {
        /* 队列没起来就退化为同步取（只在极早期发生） */
        g_music.lyric_ok = netease_lyric(id, g_music.lyric, sizeof(g_music.lyric));
        return;
    }
    lyric_req_t r = { .id = id };
    xQueueSend(s_lyric_q, &r, 0);
}

/** 歌词是否已经取回来（界面用它决定要不要重新解析） */
bool music_lyric_ready(void)
{
    return s_lyric_ready;
}

/**
 * 取歌词到调用方缓冲（线程安全）。
 * 后台任务正在写 g_music.lyric 时，这里会等它写完，不会读到半截内容。
 */
bool music_get_lyric(char *out, size_t cap)
{
    if (!out || cap == 0) return false;
    bool ok;
    if (s_lyric_lock) {
        xSemaphoreTake(s_lyric_lock, portMAX_DELAY);
    }
    ok = g_music.lyric_ok;
    strncpy(out, g_music.lyric, cap - 1);
    out[cap - 1] = '\0';
    if (s_lyric_lock) {
        xSemaphoreGive(s_lyric_lock);
    }
    return ok && out[0] != '\0';
}

void music_state_init(void)
{
    memset(&g_music, 0, sizeof(g_music));
    g_music.current = -1;
    g_music.mode = g_cfg.play_mode;
    strncpy(g_music.list_title, "每日新歌", sizeof(g_music.list_title) - 1);

    s_lyric_q = xQueueCreate(4, sizeof(lyric_req_t));
    s_lyric_lock = xSemaphoreCreateMutex();
    if (s_lyric_q && s_lyric_lock) {
        /* 栈必须在内部 RAM：PSRAM 栈上调用 flash/NVS 会直接断言崩溃 */
        if (xTaskCreate(lyric_task, "lyric", LYRIC_STACK, NULL, 3, NULL) != pdPASS) {
            ESP_LOGE(TAG, "歌词任务创建失败");
            s_lyric_q = NULL;
        }
    } else {
        ESP_LOGE(TAG, "歌词队列/锁创建失败");
        s_lyric_q = NULL;
    }
}

static void set_title(const char *s)
{
    strncpy(g_music.list_title, s, sizeof(g_music.list_title) - 1);
    g_music.list_title[sizeof(g_music.list_title) - 1] = '\0';
}

bool music_load_home(void)
{
    /* 歌单与每日新歌各自独立，一个失败不影响另一个 */
    bool ok_pl = netease_playlists(&g_music.playlists);
    if (!ok_pl) {
        g_music.playlists.count = 0;
    }
    bool ok_daily = music_load_daily();
    ESP_LOGI(TAG, "首页加载：歌单 %d 个(%s)，每日新歌 %d 首(%s)",
             g_music.playlists.count, ok_pl ? "成功" : "失败",
             g_music.songs.count, ok_daily ? "成功" : "失败");
    return ok_pl || ok_daily;
}

bool music_load_daily(void)
{
    song_list_t *tmp = &s_tmp;
    if (!netease_daily(tmp)) return false;
    g_music.songs = *tmp;
    g_music.current = tmp->count > 0 ? 0 : -1;
    set_title("每日新歌");
    return true;
}

bool music_load_playlist(int index)
{
    if (index < 0 || index >= g_music.playlists.count) return false;
    song_list_t *tmp = &s_tmp;
    if (!netease_playlist_tracks(g_music.playlists.items[index].id, tmp)) return false;
    g_music.songs = *tmp;
    g_music.current = tmp->count > 0 ? 0 : -1;
    set_title(g_music.playlists.items[index].name);
    return true;
}

bool music_load_search(const char *keyword)
{
    if (!keyword || !*keyword) return false;
    song_list_t *tmp = &s_tmp;
    if (!netease_search(keyword, 0, tmp)) return false;
    g_music.songs = *tmp;
    g_music.current = tmp->count > 0 ? 0 : -1;
    snprintf(g_music.list_title, sizeof(g_music.list_title), "搜索：%s", keyword);
    return true;
}

song_t *music_current(void)
{
    if (g_music.current < 0 || g_music.current >= g_music.songs.count) return NULL;
    return &g_music.songs.items[g_music.current];
}

const char *music_current_title(void)
{
    song_t *s = music_current();
    return s ? s->name : "暂无歌曲";
}

bool music_play_current(void)
{
    song_t *s = music_current();
    if (!s) return false;

    ESP_LOGI(TAG, "播放 %lld %s / %s", (long long)s->id, s->name, s->artist);

    /* 歌词走后台任务：在这里同步取会卡住 UI 甚至栈溢出重启 */
    s_lyric_ready = false;
    lyric_request(s->id);

    return player_play(s, g_cfg.quality);
}

bool music_next(void)
{
    if (g_music.songs.count == 0) return false;
    if (g_music.mode == MODE_SHUFFLE && g_music.songs.count > 1) {
        int n = g_music.current;
        while (n == g_music.current) {
            n = (int)(esp_random() % (uint32_t)g_music.songs.count);
        }
        g_music.current = n;
    } else {
        g_music.current = (g_music.current + 1) % g_music.songs.count;
    }
    return music_play_current();
}

bool music_prev(void)
{
    if (g_music.songs.count == 0) return false;
    g_music.current = (g_music.current - 1 + g_music.songs.count) % g_music.songs.count;
    return music_play_current();
}

void music_set_mode(int mode)
{
    if (mode < 0 || mode > 2) mode = 0;
    g_music.mode = mode;
    g_cfg.play_mode = (uint8_t)mode;
    settings_save();
}

const char *music_mode_text(void)
{
    switch (g_music.mode) {
    case MODE_LOOP_ONE: return "单曲循环";
    case MODE_SHUFFLE:  return "随机播放";
    default:            return "顺序播放";
    }
}
