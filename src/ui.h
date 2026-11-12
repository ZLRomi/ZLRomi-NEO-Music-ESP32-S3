/**
 * @file ui.h
 * @brief ZLRomi NEO Music Player - LVGL v9 UI
 * 深色主题 + 中文 + 7键焦点导航 + 推荐歌单 + 美化播放界面
 */
#pragma once
#include <lvgl.h>
#include "config.h"

// 页面状态
typedef enum {
    PAGE_MAIN = 0,      // 主页(推荐歌单+每日新歌)
    PAGE_SEARCH,        // 搜索
    PAGE_PLAYLIST,      // 歌单详情/歌曲列表
    PAGE_PLAYING,       // 播放详情
    PAGE_LYRICS,        // 歌词
    PAGE_SETTINGS,      // 设置
    PAGE_WIFI,          // WiFi配置
    PAGE_COUNT
} PageId;

// 初始化UI
void ui_init(void);

// 页面导航
void ui_navigate(PageId page);
PageId ui_current_page(void);

// 更新动态内容
void ui_update_song(const cfg_music *music);
void ui_update_progress(int pos_ms, int dur_ms);
void ui_update_lyrics(const char *prev, const char *cur, const char *next);
void ui_update_recommend_list(void);
void ui_update_search_results(void);
void ui_update_playlist(void);
void ui_show_toast(const char *msg);
void ui_set_wifi_status(bool connected);

// 焦点管理
void ui_focus_move(int delta);
int ui_focus_get(void);
