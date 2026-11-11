/**
 * @file ui.h
 * @brief ZLRomi NEO Music Player - LVGL v9 GUI模块
 * 
 * 深色主题 + 全中文界面 + 7键焦点导航 + 拼音输入法
 * 使用内置 source_han_sans_sc CJK字体
 */
#pragma once
#include "types.h"
#include <lvgl.h>

// UI初始化
void ui_init();

// 创建所有页面
void ui_create_screens();

// 页面导航
void ui_navigate_to(AppState state);

// 获取当前页面
AppState ui_get_state();

// 更新UI动态内容
void ui_update_progress(int progress_ms, int duration_ms);
void ui_update_song(const SongInfo* song);
void ui_update_lyrics(const LyricsData* lyrics, int current_line);
void ui_show_toast(const char* msg);
void ui_hide_toast();
// 检查并自动隐藏超时Toast(在loop中调用)
void ui_check_toast_timeout();

// WiFi状态图标动态更新
void ui_set_wifi_status(bool connected);

// 搜索相关
lv_obj_t* ui_get_search_textarea();
const char* ui_get_search_keyword();

// 搜索结果列表更新(可点击选择)
void ui_update_search_results(const SongInfo* results, int count);
int ui_get_search_result_count();

// 焦点管理 (7键导航)
void ui_set_focus_count(int count);
int ui_get_focus_index();
void ui_move_focus(int delta);
void ui_update_focus_highlight();

// WiFi配置页获取输入
const char* ui_get_wifi_ssid();
const char* ui_get_wifi_pass();
