/**
 * @file ui.cpp
 * @brief ZLRomi NEO Music Player - LVGL UI
 * 深色主题 + 7键焦点导航 + 拼音输入法
 * 注意: 使用英文标签避免CJK字体缺失字符问题
 */
#include "ui.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

// =====================================================================
//  颜色主题
// =====================================================================
#define C_BG       lv_color_hex(0x0A0A0A)
#define C_SURFACE  lv_color_hex(0x1A1A1A)
#define C_SURFACE2 lv_color_hex(0x2A2A2A)
#define C_PRIMARY  lv_color_hex(0xFF6B6B)
#define C_ACCENT   lv_color_hex(0x4ECDC4)
#define C_TEXT     lv_color_hex(0xFFFFFF)
#define C_TEXT_SEC lv_color_hex(0xBDBDBD)
#define C_DIM      lv_color_hex(0x666666)
#define C_FOCUS    lv_color_hex(0xFF6B6B)
#define C_SUCCESS  lv_color_hex(0x51CF66)
#define C_WARN     lv_color_hex(0xFFD43B)

#define SCREEN_W 240
#define SCREEN_H 320

// =====================================================================
//  全局UI状态
// =====================================================================
static AppState s_state = STATE_BOOT;
static int s_focus_index = 0;
static int s_focus_count = 0;
static bool s_wifi_connected = false;

// 页面对象
static lv_obj_t *s_scr_main;
static lv_obj_t *s_scr_search;
static lv_obj_t *s_scr_playing;
static lv_obj_t *s_scr_lyrics;
static lv_obj_t *s_scr_settings;
static lv_obj_t *s_scr_wifi;

// 主界面
static lv_obj_t *s_lbl_title;
static lv_obj_t *s_lbl_artist;
static lv_obj_t *s_bar_progress;
static lv_obj_t *s_lbl_time;
static lv_obj_t *s_lbl_wifi_icon;

// 搜索界面
static lv_obj_t *s_ta_search;
static lv_obj_t *s_kb;
static lv_obj_t *s_ime_pinyin;
static lv_obj_t *s_list_results;

// 播放界面
static lv_obj_t *s_lbl_p_title;
static lv_obj_t *s_lbl_p_artist;
static lv_obj_t *s_bar_p_progress;
static lv_obj_t *s_lbl_p_time;

// 歌词界面
static lv_obj_t *s_lbl_lyr_prev;
static lv_obj_t *s_lbl_lyr_cur;
static lv_obj_t *s_lbl_lyr_next;

// 设置界面
static lv_obj_t *s_settings_items[6];
static const char *s_settings_labels[] = {
    "WiFi Setup", "Volume", "Quality", "Bluetooth", "Auto Play", "About"
};
static const int SETTINGS_COUNT = 6;

// WiFi界面
static lv_obj_t *s_ta_ssid;
static lv_obj_t *s_ta_pass;
static lv_obj_t *s_lbl_wifi_status;

// Toast
static lv_obj_t *s_toast_label;

// Focus group
static lv_group_t *s_focus_group = nullptr;

// =====================================================================
//  辅助函数
// =====================================================================
static lv_obj_t* make_label(lv_obj_t *parent, const char *text,
                             const lv_font_t *font, lv_color_t color,
                             lv_align_t align, int x, int y) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    if (font) lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_align(lbl, align, x, y);
    return lbl;
}

static lv_obj_t* make_panel(lv_obj_t *parent, int w, int h,
                              lv_align_t align, int x, int y,
                              lv_color_t bg, int radius) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, w, h);
    lv_obj_align(p, align, x, y);
    lv_obj_set_style_bg_color(p, bg, 0);
    lv_obj_set_style_radius(p, radius, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

// 将对象加入焦点组，并设置可见的焦点样式
static void add_to_focus(lv_obj_t *obj) {
    if (s_focus_group && obj) {
        lv_group_add_obj(s_focus_group, obj);
        // 焦点样式: 2px珊瑚红边框 + 稍亮背景
        lv_obj_set_style_border_width(obj, 2, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(obj, C_FOCUS, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(obj, C_SURFACE2, LV_STATE_FOCUSED);
        lv_obj_set_style_outline_width(obj, 0, LV_STATE_FOCUSED);
    }
}

// =====================================================================
//  全局中文字体 (TinyTTF从SPIFFS加载)
// =====================================================================
static lv_font_t *s_cn_font_14 = nullptr;
static lv_font_t *s_cn_font_16 = nullptr;

// 获取中文字体指针（如果TinyTTF加载失败则回退到内置CJK）
static const lv_font_t* get_cn_font(int size) {
    if (size <= 14 && s_cn_font_14) return s_cn_font_14;
    if (size >= 16 && s_cn_font_16) return s_cn_font_16;
    // 回退
    return get_cn_font(16);
}

// =====================================================================
//  初始化
// =====================================================================
void ui_init() {
    s_state = STATE_BOOT;
    s_focus_index = 0;
    s_focus_count = 0;
    s_wifi_connected = false;
    
    // 创建焦点组
    s_focus_group = lv_group_create();
    lv_group_set_default(s_focus_group);
    
    // 从SPIFFS加载中文字体
#if LV_USE_TINY_TTF
    Serial.println("[UI] Loading Chinese font from SPIFFS...");
    s_cn_font_14 = lv_tiny_ttf_create_file("/spiffs/NotoSansSC-Regular.otf", 14);
    s_cn_font_16 = lv_tiny_ttf_create_file("/spiffs/NotoSansSC-Regular.otf", 16);
    if (s_cn_font_14) Serial.println("[UI] CN font 14 loaded");
    else Serial.println("[UI] CN font 14 FAILED, using fallback");
    if (s_cn_font_16) Serial.println("[UI] CN font 16 loaded");
    else Serial.println("[UI] CN font 16 FAILED, using fallback");
#endif
}

// =====================================================================
//  创建所有页面
// =====================================================================
void ui_create_screens() {
    // ==================== 主界面 ====================
    s_scr_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_main, C_BG, 0);
    lv_obj_set_style_pad_all(s_scr_main, 0, 0);
    lv_obj_clear_flag(s_scr_main, LV_OBJ_FLAG_SCROLLABLE);

    // 状态栏
    lv_obj_t *status = make_panel(s_scr_main, SCREEN_W, 28,
                                   LV_ALIGN_TOP_MID, 0, 0, C_SURFACE, 0);
    // WiFi图标（动态更新）
    s_lbl_wifi_icon = make_label(status, "---", &lv_font_montserrat_14, C_DIM,
                                  LV_ALIGN_LEFT_MID, 8, 0);
    make_label(status, "NEO Music", get_cn_font(14), C_TEXT,
               LV_ALIGN_CENTER, 0, 0);

    // 封面占位
    lv_obj_t *cover = make_panel(s_scr_main, 180, 180,
                                  LV_ALIGN_TOP_MID, 0, 38, C_SURFACE, 16);
    make_label(cover, "♪", &lv_font_montserrat_24, C_DIM,
               LV_ALIGN_CENTER, 0, 0);

    // 歌名
    s_lbl_title = lv_label_create(s_scr_main);
    lv_label_set_text(s_lbl_title, "未播放");
    lv_obj_set_style_text_font(s_lbl_title, get_cn_font(16), 0);
    lv_obj_set_style_text_color(s_lbl_title, C_TEXT, 0);
    lv_obj_align(s_lbl_title, LV_ALIGN_TOP_MID, 0, 228);
    lv_label_set_long_mode(s_lbl_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_lbl_title, 220);

    // 歌手
    s_lbl_artist = lv_label_create(s_scr_main);
    lv_label_set_text(s_lbl_artist, "");
    lv_obj_set_style_text_font(s_lbl_artist, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_artist, C_TEXT_SEC, 0);
    lv_obj_align(s_lbl_artist, LV_ALIGN_TOP_MID, 0, 250);

    // 进度条
    s_bar_progress = lv_bar_create(s_scr_main);
    lv_obj_set_size(s_bar_progress, 200, 6);
    lv_obj_align(s_bar_progress, LV_ALIGN_TOP_MID, 0, 272);
    lv_obj_set_style_bg_color(s_bar_progress, C_SURFACE2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_progress, C_PRIMARY, LV_PART_INDICATOR);
    lv_bar_set_value(s_bar_progress, 0, LV_ANIM_OFF);

    // 时间
    s_lbl_time = make_label(s_scr_main, "0:00/0:00", &lv_font_montserrat_14,
                             C_DIM, LV_ALIGN_TOP_MID, 0, 284);

    // 底部提示
    make_label(s_scr_main, "OK:Play  SET:Search  < >:Skip",
               &lv_font_montserrat_14, C_DIM, LV_ALIGN_BOTTOM_MID, 0, -6);

    // ==================== 搜索界面 ====================
    s_scr_search = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_search, C_BG, 0);
    lv_obj_set_style_pad_all(s_scr_search, 0, 0);
    lv_obj_clear_flag(s_scr_search, LV_OBJ_FLAG_SCROLLABLE);

    make_label(s_scr_search, "Search", &lv_font_montserrat_18,
               C_PRIMARY, LV_ALIGN_TOP_MID, 0, 8);

    // 搜索输入框
    s_ta_search = lv_textarea_create(s_scr_search);
    lv_obj_set_size(s_ta_search, 220, 40);
    lv_obj_align(s_ta_search, LV_ALIGN_TOP_MID, 0, 38);
    lv_textarea_set_placeholder_text(s_ta_search, "Song / Artist...");
    lv_obj_set_style_bg_color(s_ta_search, C_SURFACE, 0);
    lv_obj_set_style_text_color(s_ta_search, C_TEXT, 0);
    lv_textarea_set_one_line(s_ta_search, true);
    add_to_focus(s_ta_search);

    // 拼音输入法
    s_ime_pinyin = lv_ime_pinyin_create(s_scr_search);
    lv_obj_set_style_text_font(s_ime_pinyin, get_cn_font(16), 0);

    // 键盘
    s_kb = lv_keyboard_create(s_scr_search);
    lv_obj_set_size(s_kb, SCREEN_W, 130);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(s_kb, s_ta_search);
    lv_ime_pinyin_set_keyboard(s_ime_pinyin, s_kb);

    // 候选词面板
    lv_obj_t *cand = lv_ime_pinyin_get_cand_panel(s_ime_pinyin);
    lv_obj_set_size(cand, LV_PCT(100), 28);
    lv_obj_align_to(cand, s_kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(cand, C_SURFACE2, 0);
    lv_obj_set_style_text_color(cand, C_TEXT, 0);

    // 搜索结果区域
    s_list_results = lv_obj_create(s_scr_search);
    lv_obj_set_size(s_list_results, 220, 70);
    lv_obj_align(s_list_results, LV_ALIGN_TOP_MID, 0, 88);
    lv_obj_set_style_bg_color(s_list_results, C_SURFACE, 0);
    lv_obj_set_style_radius(s_list_results, 8, 0);
    lv_obj_set_style_border_width(s_list_results, 0, 0);

    // ==================== 播放界面 ====================
    s_scr_playing = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_playing, C_BG, 0);
    lv_obj_set_style_pad_all(s_scr_playing, 0, 0);
    lv_obj_clear_flag(s_scr_playing, LV_OBJ_FLAG_SCROLLABLE);

    // 大封面
    lv_obj_t *p_cover = make_panel(s_scr_playing, 200, 150,
                                    LV_ALIGN_TOP_MID, 0, 10, C_SURFACE, 12);
    make_label(p_cover, "♪", &lv_font_montserrat_24, C_DIM,
               LV_ALIGN_CENTER, 0, 0);

    // 歌名
    s_lbl_p_title = lv_label_create(s_scr_playing);
    lv_label_set_text(s_lbl_p_title, "");
    lv_obj_set_style_text_font(s_lbl_p_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_lbl_p_title, C_TEXT, 0);
    lv_obj_align(s_lbl_p_title, LV_ALIGN_TOP_MID, 0, 170);
    lv_label_set_long_mode(s_lbl_p_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_lbl_p_title, 220);

    // 歌手
    s_lbl_p_artist = lv_label_create(s_scr_playing);
    lv_label_set_text(s_lbl_p_artist, "");
    lv_obj_set_style_text_font(s_lbl_p_artist, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_p_artist, C_TEXT_SEC, 0);
    lv_obj_align(s_lbl_p_artist, LV_ALIGN_TOP_MID, 0, 194);

    // 进度条
    s_bar_p_progress = lv_bar_create(s_scr_playing);
    lv_obj_set_size(s_bar_p_progress, 200, 6);
    lv_obj_align(s_bar_p_progress, LV_ALIGN_TOP_MID, 0, 220);
    lv_obj_set_style_bg_color(s_bar_p_progress, C_SURFACE2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_p_progress, C_PRIMARY, LV_PART_INDICATOR);
    lv_bar_set_value(s_bar_p_progress, 0, LV_ANIM_OFF);

    // 时间
    s_lbl_p_time = make_label(s_scr_playing, "0:00/0:00",
                               &lv_font_montserrat_14, C_DIM,
                               LV_ALIGN_TOP_MID, 0, 232);

    // 控制按钮
    lv_obj_t *ctrl = lv_obj_create(s_scr_playing);
    lv_obj_set_size(ctrl, 200, 40);
    lv_obj_align(ctrl, LV_ALIGN_TOP_MID, 0, 258);
    lv_obj_set_style_bg_opa(ctrl, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(ctrl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *btn_prev = lv_button_create(ctrl);
    lv_obj_set_size(btn_prev, 50, 36);
    lv_obj_set_style_bg_color(btn_prev, C_SURFACE, 0);
    make_label(btn_prev, "<<", &lv_font_montserrat_16, C_TEXT, LV_ALIGN_CENTER, 0, 0);
    add_to_focus(btn_prev);

    lv_obj_t *btn_play = lv_button_create(ctrl);
    lv_obj_set_size(btn_play, 60, 36);
    lv_obj_set_style_bg_color(btn_play, C_PRIMARY, 0);
    make_label(btn_play, ">", &lv_font_montserrat_18, C_TEXT, LV_ALIGN_CENTER, 0, 0);
    add_to_focus(btn_play);

    lv_obj_t *btn_next = lv_button_create(ctrl);
    lv_obj_set_size(btn_next, 50, 36);
    lv_obj_set_style_bg_color(btn_next, C_SURFACE, 0);
    make_label(btn_next, ">>", &lv_font_montserrat_16, C_TEXT, LV_ALIGN_CENTER, 0, 0);
    add_to_focus(btn_next);

    make_label(s_scr_playing, "SET:Back  UP:Lyrics",
               &lv_font_montserrat_14, C_DIM, LV_ALIGN_BOTTOM_MID, 0, -6);

    // ==================== 歌词界面 ====================
    s_scr_lyrics = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_lyrics, C_BG, 0);
    lv_obj_set_style_pad_all(s_scr_lyrics, 0, 0);
    lv_obj_clear_flag(s_scr_lyrics, LV_OBJ_FLAG_SCROLLABLE);

    make_label(s_scr_lyrics, "Lyrics", &lv_font_montserrat_18,
               C_PRIMARY, LV_ALIGN_TOP_MID, 0, 10);

    s_lbl_lyr_prev = lv_label_create(s_scr_lyrics);
    lv_label_set_text(s_lbl_lyr_prev, "");
    lv_obj_set_style_text_font(s_lbl_lyr_prev, get_cn_font(14), 0);
    lv_obj_set_style_text_color(s_lbl_lyr_prev, C_DIM, 0);
    lv_obj_align(s_lbl_lyr_prev, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_width(s_lbl_lyr_prev, 220);
    lv_label_set_long_mode(s_lbl_lyr_prev, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_lbl_lyr_prev, LV_TEXT_ALIGN_CENTER, 0);

    s_lbl_lyr_cur = lv_label_create(s_scr_lyrics);
    lv_label_set_text(s_lbl_lyr_cur, "");
    lv_obj_set_style_text_font(s_lbl_lyr_cur, get_cn_font(16), 0);
    lv_obj_set_style_text_color(s_lbl_lyr_cur, C_PRIMARY, 0);
    lv_obj_align(s_lbl_lyr_cur, LV_ALIGN_TOP_MID, 0, 130);
    lv_obj_set_width(s_lbl_lyr_cur, 220);
    lv_label_set_long_mode(s_lbl_lyr_cur, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_lbl_lyr_cur, LV_TEXT_ALIGN_CENTER, 0);

    s_lbl_lyr_next = lv_label_create(s_scr_lyrics);
    lv_label_set_text(s_lbl_lyr_next, "");
    lv_obj_set_style_text_font(s_lbl_lyr_next, get_cn_font(14), 0);
    lv_obj_set_style_text_color(s_lbl_lyr_next, C_DIM, 0);
    lv_obj_align(s_lbl_lyr_next, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_set_width(s_lbl_lyr_next, 220);
    lv_label_set_long_mode(s_lbl_lyr_next, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_lbl_lyr_next, LV_TEXT_ALIGN_CENTER, 0);

    make_label(s_scr_lyrics, "SET: Back", &lv_font_montserrat_14,
               C_DIM, LV_ALIGN_BOTTOM_MID, 0, -6);

    // ==================== 设置界面 ====================
    s_scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_settings, C_BG, 0);
    lv_obj_set_style_pad_all(s_scr_settings, 0, 0);
    lv_obj_clear_flag(s_scr_settings, LV_OBJ_FLAG_SCROLLABLE);

    make_label(s_scr_settings, "Settings", &lv_font_montserrat_18,
               C_PRIMARY, LV_ALIGN_TOP_MID, 0, 10);

    for (int i = 0; i < SETTINGS_COUNT; i++) {
        s_settings_items[i] = lv_obj_create(s_scr_settings);
        lv_obj_set_size(s_settings_items[i], 220, 36);
        lv_obj_align(s_settings_items[i], LV_ALIGN_TOP_MID, 0, 42 + i * 40);
        lv_obj_set_style_bg_color(s_settings_items[i], C_SURFACE, 0);
        lv_obj_set_style_radius(s_settings_items[i], 8, 0);
        lv_obj_set_style_border_width(s_settings_items[i], 0, 0);
        lv_obj_set_style_pad_all(s_settings_items[i], 0, 0);
        lv_obj_clear_flag(s_settings_items[i], LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(s_settings_items[i]);
        lv_label_set_text(lbl, s_settings_labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, C_TEXT, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 15, 0);
        
        add_to_focus(s_settings_items[i]);
    }

    make_label(s_scr_settings, "UP/DN:Select  OK:Confirm  SET:Back",
               &lv_font_montserrat_14, C_DIM, LV_ALIGN_BOTTOM_MID, 0, -6);

    // ==================== WiFi配置界面 ====================
    s_scr_wifi = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_wifi, C_BG, 0);
    lv_obj_set_style_pad_all(s_scr_wifi, 0, 0);
    lv_obj_clear_flag(s_scr_wifi, LV_OBJ_FLAG_SCROLLABLE);

    make_label(s_scr_wifi, "WiFi Setup", &lv_font_montserrat_18,
               C_PRIMARY, LV_ALIGN_TOP_MID, 0, 10);

    make_label(s_scr_wifi, "SSID:", &lv_font_montserrat_14,
               C_TEXT_SEC, LV_ALIGN_TOP_LEFT, 15, 42);
    s_ta_ssid = lv_textarea_create(s_scr_wifi);
    lv_obj_set_size(s_ta_ssid, 210, 36);
    lv_obj_align(s_ta_ssid, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_color(s_ta_ssid, C_SURFACE, 0);
    lv_obj_set_style_text_color(s_ta_ssid, C_TEXT, 0);
    lv_textarea_set_one_line(s_ta_ssid, true);
    add_to_focus(s_ta_ssid);

    make_label(s_scr_wifi, "Password:", &lv_font_montserrat_14,
               C_TEXT_SEC, LV_ALIGN_TOP_LEFT, 15, 105);
    s_ta_pass = lv_textarea_create(s_scr_wifi);
    lv_obj_set_size(s_ta_pass, 210, 36);
    lv_obj_align(s_ta_pass, LV_ALIGN_TOP_MID, 0, 123);
    lv_obj_set_style_bg_color(s_ta_pass, C_SURFACE, 0);
    lv_obj_set_style_text_color(s_ta_pass, C_TEXT, 0);
    lv_textarea_set_one_line(s_ta_pass, true);
    lv_textarea_set_password_mode(s_ta_pass, true);
    add_to_focus(s_ta_pass);

    lv_obj_t *btn_conn = lv_button_create(s_scr_wifi);
    lv_obj_set_size(btn_conn, 120, 36);
    lv_obj_align(btn_conn, LV_ALIGN_TOP_MID, 0, 178);
    lv_obj_set_style_bg_color(btn_conn, C_PRIMARY, 0);
    make_label(btn_conn, "Connect", &lv_font_montserrat_14, C_TEXT, LV_ALIGN_CENTER, 0, 0);
    add_to_focus(btn_conn);

    s_lbl_wifi_status = make_label(s_scr_wifi, "", &lv_font_montserrat_14,
                                    C_DIM, LV_ALIGN_TOP_MID, 0, 225);

    make_label(s_scr_wifi, "SET: Back", &lv_font_montserrat_14,
               C_DIM, LV_ALIGN_BOTTOM_MID, 0, -6);

    // ==================== Toast ====================
    s_toast_label = lv_label_create(lv_layer_top());
    lv_obj_set_style_bg_color(s_toast_label, C_SURFACE2, 0);
    lv_obj_set_style_text_color(s_toast_label, C_TEXT, 0);
    lv_obj_set_style_pad_all(s_toast_label, 8, 0);
    lv_obj_set_style_radius(s_toast_label, 8, 0);
    lv_obj_add_flag(s_toast_label, LV_OBJ_FLAG_HIDDEN);
}

// =====================================================================
//  页面导航
// =====================================================================
void ui_navigate_to(AppState state) {
    s_state = state;
    s_focus_index = 0;

    // 清除当前group中的所有对象
    if (s_focus_group) {
        lv_group_remove_all_objs(s_focus_group);
    }

    switch (state) {
        case STATE_MAIN:
            lv_scr_load(s_scr_main);
            s_focus_count = 1;
            break;
        case STATE_SEARCH:
            lv_scr_load(s_scr_search);
            lv_textarea_set_text(s_ta_search, "");
            // 显示键盘并关联textarea
            lv_keyboard_set_textarea(s_kb, s_ta_search);
            lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
            // keyboard加入group并设为焦点（keyboard会自动转发输入到textarea）
            add_to_focus(s_kb);
            lv_group_focus_obj(s_kb);
            s_focus_count = 1;
            break;
        case STATE_PLAYING:
            lv_scr_load(s_scr_playing);
            s_focus_count = 3;
            break;
        case STATE_LYRICS:
            lv_scr_load(s_scr_lyrics);
            s_focus_count = 0;
            break;
        case STATE_SETTINGS:
            lv_scr_load(s_scr_settings);
            s_focus_count = SETTINGS_COUNT;
            for (int i = 0; i < SETTINGS_COUNT; i++) {
                add_to_focus(s_settings_items[i]);
            }
            ui_update_focus_highlight();
            break;
        case STATE_WIFI:
            lv_scr_load(s_scr_wifi);
            add_to_focus(s_ta_ssid);
            add_to_focus(s_ta_pass);
            s_focus_count = 3;
            break;
        default:
            break;
    }
}

AppState ui_get_state() {
    return s_state;
}

// =====================================================================
//  WiFi状态更新
// =====================================================================
void ui_set_wifi_status(bool connected) {
    s_wifi_connected = connected;
    if (s_lbl_wifi_icon) {
        if (connected) {
            lv_label_set_text(s_lbl_wifi_icon, "[W]");
            lv_obj_set_style_text_color(s_lbl_wifi_icon, C_SUCCESS, 0);
        } else {
            lv_label_set_text(s_lbl_wifi_icon, "[--]");
            lv_obj_set_style_text_color(s_lbl_wifi_icon, C_DIM, 0);
        }
    }
}

// =====================================================================
//  动态更新
// =====================================================================
void ui_update_progress(int progress_ms, int duration_ms) {
    if (duration_ms <= 0) return;

    int pct = (progress_ms * 100) / duration_ms;
    lv_bar_set_value(s_bar_progress, pct, LV_ANIM_OFF);
    lv_bar_set_value(s_bar_p_progress, pct, LV_ANIM_OFF);

    char buf[20];
    snprintf(buf, sizeof(buf), "%d:%02d/%d:%02d",
             progress_ms / 60000, (progress_ms / 1000) % 60,
             duration_ms / 60000, (duration_ms / 1000) % 60);
    lv_label_set_text(s_lbl_time, buf);
    lv_label_set_text(s_lbl_p_time, buf);
}

void ui_update_song(const SongInfo* song) {
    if (!song) return;
    lv_label_set_text(s_lbl_title, song->name);
    lv_label_set_text(s_lbl_artist, song->artist);
    lv_label_set_text(s_lbl_p_title, song->name);
    lv_label_set_text(s_lbl_p_artist, song->artist);
}

void ui_update_lyrics(const LyricsData* lyrics, int current_line) {
    if (!lyrics || !lyrics->has_lyrics || current_line < 0) {
        lv_label_set_text(s_lbl_lyr_prev, "");
        lv_label_set_text(s_lbl_lyr_cur, "No lyrics");
        lv_label_set_text(s_lbl_lyr_next, "");
        return;
    }

    if (current_line > 0)
        lv_label_set_text(s_lbl_lyr_prev, lyrics->lines[current_line - 1].text);
    else
        lv_label_set_text(s_lbl_lyr_prev, "");

    lv_label_set_text(s_lbl_lyr_cur, lyrics->lines[current_line].text);

    if (current_line < lyrics->count - 1)
        lv_label_set_text(s_lbl_lyr_next, lyrics->lines[current_line + 1].text);
    else
        lv_label_set_text(s_lbl_lyr_next, "");
}

void ui_show_toast(const char* msg) {
    lv_label_set_text(s_toast_label, msg);
    lv_obj_clear_flag(s_toast_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_toast_label, LV_ALIGN_BOTTOM_MID, 0, -40);
}

void ui_update_search_results(const SongInfo* results, int count) {
    lv_obj_clean(s_list_results);
    for (int i = 0; i < count && i < 5; i++) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s - %s", results[i].name, results[i].artist);
        lv_obj_t *lbl = lv_label_create(s_list_results);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, C_TEXT, 0);
        lv_obj_set_width(lbl, 200);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    }
}

// =====================================================================
//  搜索相关
// =====================================================================
lv_obj_t* ui_get_search_textarea() { return s_ta_search; }
const char* ui_get_search_keyword() { return lv_textarea_get_text(s_ta_search); }

// =====================================================================
//  焦点管理
// =====================================================================
void ui_set_focus_count(int count) { s_focus_count = count; }
int ui_get_focus_index() { return s_focus_index; }

void ui_move_focus(int delta) {
    if (s_focus_count <= 0) return;
    s_focus_index += delta;
    if (s_focus_index < 0) s_focus_index = s_focus_count - 1;
    if (s_focus_index >= s_focus_count) s_focus_index = 0;
    ui_update_focus_highlight();
}

void ui_update_focus_highlight() {
    if (s_state == STATE_SETTINGS) {
        for (int i = 0; i < SETTINGS_COUNT; i++) {
            lv_obj_set_style_border_width(s_settings_items[i], 0, 0);
            lv_obj_set_style_bg_color(s_settings_items[i], C_SURFACE, 0);
        }
        if (s_focus_index >= 0 && s_focus_index < SETTINGS_COUNT) {
            lv_obj_set_style_border_width(s_settings_items[s_focus_index], 2, 0);
            lv_obj_set_style_border_color(s_settings_items[s_focus_index], C_FOCUS, 0);
            lv_obj_set_style_bg_color(s_settings_items[s_focus_index], C_SURFACE2, 0);
        }
    }
}
