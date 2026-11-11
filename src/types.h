/**
 * @file types.h
 * @brief ZLRomi NEO Music Player - 全局类型定义
 * 所有模块共用此文件中的类型，避免重复定义
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

// ---- 音质档位 ----
typedef enum {
    QUALITY_STANDARD = 0,   // 128k
    QUALITY_HIGH     = 1,   // 320k
    QUALITY_LOSSLESS = 2,   // 无损
    QUALITY_COUNT    = 3
} AudioQuality;

// ---- 播放状态 ----
typedef enum {
    PLAY_STOPPED = 0,
    PLAY_PLAYING,
    PLAY_PAUSED,
    PLAY_BUFFERING,
    PLAY_ERROR
} PlayState;

// ---- 页面状态 ----
typedef enum {
    STATE_BOOT = 0,
    STATE_MAIN,
    STATE_SEARCH,
    STATE_PLAYING,
    STATE_LYRICS,
    STATE_SETTINGS,
    STATE_WIFI
} AppState;

// ---- 按键ID ----
typedef enum {
    BTN_UP = 0, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
    BTN_OK, BTN_SET, BTN_RST, BTN_COUNT
} ButtonId;

// ---- 按键事件 ----
typedef enum {
    EVT_NONE = 0, EVT_PRESS, EVT_LONG_PRESS, EVT_RELEASE
} ButtonEvent;

// ---- 歌曲信息 ----
typedef struct {
    int64_t id;
    char name[128];
    char artist[128];
    char album[128];
    char cover_url[256];
    int duration_ms;
    int progress_ms;
    bool playing;
} SongInfo;

// ---- 播放URL ----
typedef struct {
    char url[512];
    int bitrate;
    int size;
    bool valid;
} UrlInfo;

// ---- 歌词行 ----
typedef struct {
    int time_ms;
    char text[256];
} LyricLine;

// ---- 歌词数据 ----
typedef struct {
    LyricLine lines[128];   // 减小到128行，节省DRAM
    int count;
    bool has_lyrics;
} LyricsData;

// ---- 设置 ----
typedef struct {
    uint8_t volume;             // 0-100
    bool bt_enabled;
    char wifi_ssid[64];
    char wifi_pass[64];
    AudioQuality quality;
    bool auto_play;
    uint8_t screen_brightness;  // 0-255
} Settings;
