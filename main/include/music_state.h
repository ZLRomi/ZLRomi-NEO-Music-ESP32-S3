/*
 * music_state.h —— 全局音乐状态（列表、当前曲目、播放模式、歌词）
 *
 * 各界面共享同一份数据，避免首页/歌单/播放页各拉一次 API。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "netease.h"

typedef enum {
    MODE_ORDER = 0,
    MODE_LOOP_ONE,
    MODE_SHUFFLE,
} play_mode_t;

typedef struct {
    playlist_list_t playlists;
    song_list_t     songs;
    char            list_title[64];
    int             current;
    int             mode;
    char            lyric[LYRIC_MAX];
    bool            lyric_ok;
    /* ── 分页 ──────────────────────────────────────────────
       列表按页加载（每页 SONG_PAGE 首），避免一次拉整张歌单
       在 ESP32 上要 TLS + cJSON 解析好几秒。 */
    int      page;        /* 当前页码，从 0 开始 */
    int      page_count;  /* 总页数；-1 = 还没探到底 */
    int64_t  src_id;      /* 列表来源：歌单 id，-1 = 每日新歌，-2 = 搜索 */
} music_state_t;

extern music_state_t g_music;

void music_state_init(void);

/* ── 异步加载（「正在加载」页用） ──────────────────────────────
   同步版在 UI 主任务里发 HTTPS，界面冻住好几秒还没提示；
   异步版投递给后台任务，UI 去加载页轮询状态。 */
typedef enum {
    LOAD_IDLE = 0,
    LOAD_BUSY,
    LOAD_OK,
    LOAD_FAIL,
} load_status_t;

void music_load_playlist_async(int index);
void music_load_daily_async(void);
void music_load_search_async(const char *keyword);
void music_load_home_async(void);
load_status_t music_load_status(void);

/* ── 翻页 ──────────────────────────────────────────────────
   列表页底部「上一页 / 下一页」。超出范围时返回 false 并给出提示。
   翻页也是异步的：置 LOAD_BUSY，net_task 拉完自动回到列表。 */
bool music_next_page(void);
bool music_prev_page(void);
int  music_page(void);        /* 当前页码，从 0 开始 */
int  music_page_count(void);  /* 总页数；-1 = 未探到底 */
bool music_page_busy(void);   /* 翻页请求进行中 */

song_t *music_current(void);
const char *music_current_title(void);
bool music_play_current(void);              /* 起播（歌词走后台，不阻塞） */
bool music_next(void);
bool music_prev(void);
void music_set_mode(int mode);
const char *music_mode_text(void);

/** 歌词是否已取回（播放页据此决定要不要重新解析歌词） */
bool music_lyric_ready(void);
/** 线程安全地取歌词文本；返回 false 表示没有歌词 */
bool music_get_lyric(char *out, size_t cap);
