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

/* net_task 用的临时列表缓冲（PSRAM），供各请求写入后再拷给 g_music.songs */
static EXT_RAM_BSS_ATTR song_list_t s_load_buf;

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
      这正是「一点歌就闪屏重启」的根因（NVS 就在 flash 上）。

    12KB 是上限：内部 RAM 非常紧（WiFi/LWIP + 音频任务 24KB + 封面任务 12KB
    + I2S DMA + TLS 握手都要从这里出）。歌词、列表加载、搜索共用这一个任务，
    串行处理，不开第二个 —— 每多一个任务栈就是少一分 TLS 的存活空间。 */
#define NET_STACK  (12 * 1024)

/* 一个队列处理所有网络请求：歌词 / 歌单 / 每日新歌 / 搜索 */
typedef enum {
    NREQ_LYRIC = 0,
    NREQ_PLAYLIST,      /* index = 歌单下标，offset = 0 */
    NREQ_DAILY,         /* offset = 第几首起 */
    NREQ_SEARCH,        /* keyword + offset */
    NREQ_HOME,          /* 开机/重连：只拉歌单列表 */
    NREQ_PAGE,          /* 翻页：index = 歌单下标（-1=每日新歌），offset = 第几首起 */
} net_kind_t;

typedef struct {
    net_kind_t kind;
    int64_t    id;            /* NREQ_LYRIC：歌曲 id */
    int        index;         /* 歌单下标；NREQ_PAGE 里 -1 表示每日新歌 */
    int        offset;        /* 起始曲目序号 */
    char       keyword[64];   /* NREQ_SEARCH */
} net_req_t;

static QueueHandle_t       s_net_q;
static volatile bool       s_lyric_ready;
static volatile load_status_t s_load_status = LOAD_IDLE;
/* 歌词与列表都由后台任务写、UI 读，用互斥锁防止读到半截内容 */
static SemaphoreHandle_t   s_net_lock;

/* 拉到的列表先落在这里，成功后才整块拷给 g_music.songs */
static EXT_RAM_BSS_ATTR song_list_t s_load_buf;
static EXT_RAM_BSS_ATTR char        s_lrc_buf[LYRIC_MAX];

/* set_title 定义在后面，这里先声明 */
static void set_title(const char *s);

/**
 * 唯一的网络任务：串行处理歌词、歌单曲目、每日新歌、搜索。
 *
 * 所有请求都是 HTTPS，TLS 握手要求调用者的栈在内部 RAM，且需要连续的内存。
 * 所以这里只开一个任务、一个 12KB 栈，让所有网络工作排队走。
 */
static void net_task(void *arg)
{
    (void)arg;
    net_req_t req;

    for (;;) {
        if (xQueueReceive(s_net_q, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (req.kind == NREQ_LYRIC) {
            ESP_LOGI(TAG, "后台取歌词 id=%lld", (long long)req.id);
            bool ok = netease_lyric(req.id, s_lrc_buf, sizeof(s_lrc_buf));

            xSemaphoreTake(s_net_lock, portMAX_DELAY);
            if (ok) {
                strncpy(g_music.lyric, s_lrc_buf, sizeof(g_music.lyric) - 1);
                g_music.lyric[sizeof(g_music.lyric) - 1] = '\0';
                g_music.lyric_ok = true;
            } else {
                g_music.lyric[0] = '\0';
                g_music.lyric_ok = false;
            }
            xSemaphoreGive(s_net_lock);

            if (ok) {
                ESP_LOGI(TAG, "歌词就绪 id=%lld", (long long)req.id);
            } else {
                ESP_LOGW(TAG, "歌词没取到 id=%lld", (long long)req.id);
            }
            /* 无论成功失败都要置位：否则播放页会一直停在「歌词加载中」 */
            s_lyric_ready = true;
            ui_redraw();
            continue;
        }

        /* 以下都是「列表类」请求：走加载页那套状态机 */
        s_load_status = LOAD_BUSY;
        bool ok = false;
        int  idx = 0;

        if (req.kind == NREQ_HOME) {
            /* 开机/重连：只拉歌单列表。每日新歌不在这里预拉 ——
               用户点进去时走加载页现拉，开机能快一大截。 */
            bool ok_pl = netease_playlists(&g_music.playlists);
            if (!ok_pl) {
                g_music.playlists.count = 0;
            }
            ESP_LOGI(TAG, "首页加载：歌单 %d 个(%s)",
                     g_music.playlists.count, ok_pl ? "成功" : "失败");
            s_load_status = ok_pl ? LOAD_OK : LOAD_FAIL;
            ui_redraw();
            continue;
        }

        if (req.kind == NREQ_DAILY) {
            ok = netease_daily(req.offset, &s_load_buf);
        } else if (req.kind == NREQ_SEARCH) {
            ok = netease_search(req.keyword, req.offset, &s_load_buf);
        } else if (req.kind == NREQ_PAGE) {
            /* 翻页：回到当前列表所在的那张歌单 / 每日新歌，取指定 offset */
            if (req.index >= 0 && req.index < g_music.playlists.count) {
                ok = netease_playlist_tracks(
                        g_music.playlists.items[req.index].id, req.offset,
                        &s_load_buf);
                idx = req.index;
            } else {
                ok = netease_daily(req.offset, &s_load_buf);
            }
        } else if (req.index >= 0 && req.index < g_music.playlists.count) {
            /* NREQ_PLAYLIST：首页点进来的那一页 */
            ok = netease_playlist_tracks(
                    g_music.playlists.items[req.index].id, 0, &s_load_buf);
            idx = req.index;
        }

        if (ok && s_load_buf.count > 0) {
            /* 拷贝期间上锁，UI 侧不会读到半截列表 */
            xSemaphoreTake(s_net_lock, portMAX_DELAY);
            g_music.songs = s_load_buf;
            g_music.current = 0;

            /* 页码状态：请求里带的是 offset，反推出页码 */
            int page = req.offset / SONG_PAGE;
            g_music.page = page;
            if (req.kind == NREQ_DAILY || req.kind == NREQ_HOME) {
                g_music.src_id = -1;
            } else if (req.kind == NREQ_SEARCH) {
                g_music.src_id = -2;
            } else if (idx < g_music.playlists.count) {
                g_music.src_id = g_music.playlists.items[idx].id;
            }
            /* 本页没装满 = 已经是最后一页：把总页数钉住。
               接口不返回 total，只能这样探测。 */
            if (s_load_buf.count < SONG_PAGE && g_music.page_count < 0) {
                g_music.page_count = page + 1;
            }

            if (req.kind == NREQ_DAILY) {
                set_title("每日新歌");
            } else if (req.kind == NREQ_SEARCH) {
                /* t 留够余量：「搜索：」是 3 个汉字共 9 字节，关键词最多 63 字节，
                   合起来可能超过 list_title 的 64 —— 所以这里放到 80，
                   由 set_title 负责最终截断，免得 -Werror=format-truncation 报错。 */
                char t[80];
                snprintf(t, sizeof(t), "搜索：%s", req.keyword);
                set_title(t);
            } else if (idx < g_music.playlists.count) {
                set_title(g_music.playlists.items[idx].name);
            }
            xSemaphoreGive(s_net_lock);
            s_load_status = LOAD_OK;
            ESP_LOGI(TAG, "列表加载成功：第 %d 页（第 %d 首起）%d 首",
                     page, req.offset, g_music.songs.count);
        } else {
            s_load_status = LOAD_FAIL;
            ESP_LOGW(TAG, "列表加载失败（offset=%d）", req.offset);
        }
        /* 通知界面：加载页据此跳转或提示 */
        ui_redraw();
    }
}

static void net_post(net_req_t *r)
{
    if (!s_net_q) return;
    xQueueSend(s_net_q, r, 0);
}

static void lyric_request(int64_t id)
{
    if (!s_net_q) {
        /* 队列没起来就退化为同步取（只在极早期发生） */
        g_music.lyric_ok = netease_lyric(id, g_music.lyric, sizeof(g_music.lyric));
        return;
    }
    net_req_t r;
    memset(&r, 0, sizeof(r));
    r.kind = NREQ_LYRIC;
    r.id = id;
    net_post(&r);
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
    /* 和 net_task 用同一把锁：歌词写、歌词读、列表写都过它 */
    if (s_net_lock) {
        xSemaphoreTake(s_net_lock, portMAX_DELAY);
    }
    ok = g_music.lyric_ok;
    strncpy(out, g_music.lyric, cap - 1);
    out[cap - 1] = '\0';
    if (s_net_lock) {
        xSemaphoreGive(s_net_lock);
    }
    return ok && out[0] != '\0';
}


void music_load_playlist_async(int index)
{
    net_req_t r;
    memset(&r, 0, sizeof(r));
    r.kind = NREQ_PLAYLIST;
    r.index = index;
    net_post(&r);
}

void music_load_daily_async(void)
{
    net_req_t r;
    memset(&r, 0, sizeof(r));
    r.kind = NREQ_DAILY;
    net_post(&r);
}

load_status_t music_load_status(void)
{
    return s_load_status;
}

/**
 * 开机/重连后拉首页数据（推荐歌单 + 每日新歌）。
 *
 * 以前这是在 main.c 里单独起一个 16KB 栈的 load_home_task —— 白占 16KB 内部 RAM。
 * 现在直接投递给网络任务串行处理，一个任务都不多开。
 */
void music_load_home_async(void)
{
    net_req_t r;
    memset(&r, 0, sizeof(r));
    r.kind = NREQ_HOME;
    net_post(&r);
}

void music_load_search_async(const char *keyword)
{
    if (!s_net_q || !keyword || !*keyword) return;
    net_req_t r;
    memset(&r, 0, sizeof(r));
    r.kind = NREQ_SEARCH;
    strncpy(r.keyword, keyword, sizeof(r.keyword) - 1);
    net_post(&r);
}

/* ── 翻页 ──────────────────────────────────────────────────
 *
 * 接口不返回 total，总页数靠探测：翻到不满一页就是最后一页。
 * 所以「下一页」永远允许尝试（失败会停在这一页并提示），
 * 「上一页」在 page==0 时直接拒绝。
 *
 * 当前列表来自哪张歌单，记在 g_music.src_id 里（-1 = 每日新歌，-2 = 搜索）。
 * 翻页前先用它反查出歌单下标，找不到就说明数据没了，直接放弃。
 */
static int src_playlist_index(void)
{
    if (g_music.src_id < 0) {
        return -1;
    }
    for (int i = 0; i < g_music.playlists.count; i++) {
        if (g_music.playlists.items[i].id == g_music.src_id) {
            return i;
        }
    }
    return -2;   /* 歌单列表换了 / 该歌单已不在榜上 */
}

static bool page_turn(int new_page)
{
    if (!s_net_q) return false;
    if (s_load_status == LOAD_BUSY) return false;      /* 上次还没翻完 */
    if (new_page < 0) return false;

    int idx = src_playlist_index();
    if (idx == -2) {
        ESP_LOGW(TAG, "翻页失败：当前歌单不在列表里");
        return false;
    }

    net_req_t r;
    memset(&r, 0, sizeof(r));
    r.kind   = NREQ_PAGE;
    r.index  = idx;
    r.offset = new_page * SONG_PAGE;
    net_post(&r);
    return true;
}

bool music_next_page(void)
{
    /* 已知总页数且已在末页：别白跑一趟 */
    if (g_music.page_count >= 0 && g_music.page + 1 >= g_music.page_count) {
        return false;
    }
    return page_turn(g_music.page + 1);
}

bool music_prev_page(void)
{
    if (g_music.page <= 0) return false;
    return page_turn(g_music.page - 1);
}

int music_page(void)
{
    return g_music.page;
}

int music_page_count(void)
{
    return g_music.page_count;
}

bool music_page_busy(void)
{
    return s_load_status == LOAD_BUSY;
}

void music_state_init(void)
{
    memset(&g_music, 0, sizeof(g_music));
    g_music.current = -1;
    g_music.page = 0;
    g_music.page_count = -1;      /* -1 = 还没探到底 */
    g_music.src_id = -1;          /* -1 = 每日新歌 */
    g_music.mode = g_cfg.play_mode;
    strncpy(g_music.list_title, "每日新歌", sizeof(g_music.list_title) - 1);

    /* 只有一个队列、一个任务：歌词 / 歌单 / 每日新歌 / 搜索全部串行走它 */
    s_net_q = xQueueCreate(6, sizeof(net_req_t));
    s_net_lock = xSemaphoreCreateMutex();
    if (s_net_q && s_net_lock) {
        ESP_LOGI(TAG, "建网络任务：需 %d 字节，内部最大块 %u",
                 (int)NET_STACK,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        if (xTaskCreate(net_task, "net", NET_STACK, NULL, 3, NULL) != pdPASS) {
            ESP_LOGE(TAG, "网络任务创建失败（内部 RAM 最大块不够 %d 字节）",
                     (int)NET_STACK);
            s_net_q = NULL;
        }
    } else {
        ESP_LOGE(TAG, "网络队列/锁创建失败");
        s_net_q = NULL;
    }
}

static void set_title(const char *s)
{
    strncpy(g_music.list_title, s, sizeof(g_music.list_title) - 1);
    g_music.list_title[sizeof(g_music.list_title) - 1] = '\0';
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
