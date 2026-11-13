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
} music_state_t;

extern music_state_t g_music;

void music_state_init(void);

bool music_load_home(void);                 /* 推荐歌单 + 每日新歌 */
bool music_load_daily(void);
bool music_load_playlist(int index);
bool music_load_search(const char *keyword);

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
