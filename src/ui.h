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

// 搜索相关
lv_obj_t* ui_get_search_textarea();
const char* ui_get_search_keyword();

// 搜索结果列表更新
void ui_update_search_results(const SongInfo* results, int count);

// 焦点管理
void ui_set_focus_count(int count);
int ui_get_focus_index();
void ui_move_focus(int delta);
void ui_update_focus_highlight();
