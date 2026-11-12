/**
 * @file ui.cpp
 * @brief ZLRomi NEO Music Player - LVGL v9 UI Implementation
 * 深色主题 + 全中文 + 7键焦点导航 + 推荐歌单封面 + 美化播放界面
 */
#include "ui.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

// =====================================================================
//  颜色主题 (深色精致风)
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

#define SCR_W 240
#define SCR_H 320

// =====================================================================
//  全局状态
// =====================================================================
static PageId s_page = PAGE_MAIN;
static int s_focus_idx = 0;
static int s_focus_max = 0;
static bool s_wifi_ok = false;

static lv_group_t *s_group = NULL;

// Screens
static lv_obj_t *s_scr[PAGE_COUNT];

// Main page objects
static lv_obj_t *s_lbl_wifi;
static lv_obj_t *s_rec_list;       // 推荐歌单列表
static lv_obj_t *s_new_list;       // 每日新歌列表

// Playing page objects
static lv_obj_t *s_lbl_p_title;
static lv_obj_t *s_lbl_p_artist;
static lv_obj_t *s_bar_progress;
static lv_obj_t *s_lbl_time;
static lv_obj_t *s_lbl_play_icon;
static lv_obj_t *s_cover_img;     // 封面图

// Lyrics page
static lv_obj_t *s_lbl_lyr_prev;
static lv_obj_t *s_lbl_lyr_cur;
static lv_obj_t *s_lbl_lyr_next;

// Search page
static lv_obj_t *s_ta_search;
static lv_obj_t *s_kb;
static lv_obj_t *s_search_results;

// Settings page
static lv_obj_t *s_set_items[6];
static const char *s_set_labels[] = {
    "WiFi\xe8\xae\xbe\xe7\xbd\xae",      // WiFi设置
    "\xe9\x9f\xb3\xe9\x87\x8f",           // 音量
    "\xe9\x9f\xb3\xe8\xb4\xa8",           // 音质
    "\xe8\x93\x9d\xe7\x89\x99",           // 蓝牙
    "\xe8\x87\xaa\xe5\x8a\xa8\xe6\x92\xad\xe6\x94\xbe", // 自动播放
    "\xe5\x85\xb3\xe4\xba\x8e"            // 关于
};

// WiFi page
static lv_obj_t *s_ta_ssid;
static lv_obj_t *s_ta_pass;
static lv_obj_t *s_lbl_wifi_st;

// Toast
static lv_obj_t *s_toast;

// =====================================================================
//  Helper functions
// =====================================================================
static lv_obj_t* mk_label(lv_obj_t *parent, const char *text,
                           const lv_font_t *font, lv_color_t color,
                           lv_align_t align, int x, int y) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    if (font) lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_align(lbl, align, x, y);
    return lbl;
}

static lv_obj_t* mk_panel(lv_obj_t *parent, int w, int h,
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

static void add_focus(lv_obj_t *obj) {
    if (s_group && obj) {
        lv_group_add_obj(s_group, obj);
        lv_obj_set_style_border_width(obj, 2, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(obj, C_FOCUS, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(obj, C_SURFACE2, LV_STATE_FOCUSED);
    }
}

// Get CJK font (fallback to built-in)
static const lv_font_t* cn_font(int size) {
    if (size <= 14) return &lv_font_source_han_sans_sc_14_cjk;
    return &lv_font_source_han_sans_sc_16_cjk;
}

// =====================================================================
//  Page: MAIN (推荐歌单 + 每日新歌)
// =====================================================================
static void create_main_page() {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    s_scr[PAGE_MAIN] = scr;

    // Status bar
    lv_obj_t *bar = mk_panel(scr, SCR_W, 26, LV_ALIGN_TOP_MID, 0, 0, C_SURFACE, 0);
    s_lbl_wifi = mk_label(bar, "[--]", &lv_font_montserrat_14, C_DIM, LV_ALIGN_LEFT_MID, 8, 0);
    mk_label(bar, "NEO \xe9\x9f\xb3\xe4\xb9\x90", cn_font(14), C_TEXT, LV_ALIGN_CENTER, 0, 0);

    // Title: 推荐歌单
    mk_label(scr, "\xe6\x8e\xa8\xe8\x8d\x90\xe6\xad\x8c\xe5\x8d\x95", cn_font(16), C_PRIMARY,
             LV_ALIGN_TOP_LEFT, 10, 32);

    // Recommend list (scrollable)
    s_rec_list = lv_list_create(scr);
    lv_obj_set_size(s_rec_list, SCR_W - 20, 120);
    lv_obj_align(s_rec_list, LV_ALIGN_TOP_MID, 0, 55);
    lv_obj_set_style_bg_color(s_rec_list, C_SURFACE, 0);
    lv_obj_set_style_radius(s_rec_list, 8, 0);
    lv_obj_set_style_border_width(s_rec_list, 0, 0);
    add_focus(s_rec_list);

    // Title: 每日新歌
    mk_label(scr, "\xe6\xaf\x8f\xe6\x97\xa5\xe6\x96\xb0\xe6\xad\x8c", cn_font(16), C_ACCENT,
             LV_ALIGN_TOP_LEFT, 10, 182);

    // New songs list
    s_new_list = lv_list_create(scr);
    lv_obj_set_size(s_new_list, SCR_W - 20, 100);
    lv_obj_align(s_new_list, LV_ALIGN_TOP_MID, 0, 205);
    lv_obj_set_style_bg_color(s_new_list, C_SURFACE, 0);
    lv_obj_set_style_radius(s_new_list, 8, 0);
    lv_obj_set_style_border_width(s_new_list, 0, 0);
    add_focus(s_new_list);

    // Bottom hint
    mk_label(scr, "OK:\xe6\x92\xad\xe6\x94\xbe SET:\xe6\x90\x9c\xe7\xb4\xa2 DN:\xe8\xae\xbe\xe7\xbd\xae",
             cn_font(14), C_DIM, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// =====================================================================
//  Page: SEARCH
// =====================================================================
static void create_search_page() {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    s_scr[PAGE_SEARCH] = scr;

    mk_label(scr, "\xe6\x90\x9c\xe7\xb4\xa2", cn_font(18), C_PRIMARY, LV_ALIGN_TOP_MID, 0, 8);

    s_ta_search = lv_textarea_create(scr);
    lv_obj_set_size(s_ta_search, 220, 36);
    lv_obj_align(s_ta_search, LV_ALIGN_TOP_MID, 0, 38);
    lv_textarea_set_placeholder_text(s_ta_search, "\xe8\xbe\x93\xe5\x85\xa5\xe6\xad\x8c\xe5\x90\x8d/\xe6\xad\x8c\xe6\x89\x8b...");
    lv_obj_set_style_bg_color(s_ta_search, C_SURFACE, 0);
    lv_obj_set_style_text_color(s_ta_search, C_TEXT, 0);
    lv_obj_set_style_text_font(s_ta_search, cn_font(14), 0);
    lv_textarea_set_one_line(s_ta_search, true);
    add_focus(s_ta_search);

    // Keyboard with pinyin IME
    s_kb = lv_keyboard_create(scr);
    lv_obj_set_size(s_kb, SCR_W, 130);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(s_kb, s_ta_search);

#if LV_USE_IME_PINYIN
    // Pinyin IME for Chinese input
    static lv_obj_t *ime = NULL;
    ime = lv_ime_pinyin_create(scr);
    lv_obj_set_style_text_font(ime, cn_font(16), 0);
    lv_ime_pinyin_set_keyboard(ime, s_kb);
    // Candidate panel above keyboard
    lv_obj_t *cand = lv_ime_pinyin_get_cand_panel(ime);
    lv_obj_set_size(cand, LV_PCT(100), 28);
    lv_obj_align_to(cand, s_kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(cand, C_SURFACE2, 0);
    lv_obj_set_style_text_color(cand, C_TEXT, 0);
#endif

    // Search results
    s_search_results = lv_list_create(scr);
    lv_obj_set_size(s_search_results, 220, 80);
    lv_obj_align(s_search_results, LV_ALIGN_TOP_MID, 0, 82);
    lv_obj_set_style_bg_color(s_search_results, C_SURFACE, 0);
    lv_obj_set_style_radius(s_search_results, 8, 0);
    lv_obj_set_style_border_width(s_search_results, 0, 0);
    add_focus(s_search_results);
}

// =====================================================================
//  Page: PLAYLIST (歌曲列表带封面)
// =====================================================================
static void create_playlist_page() {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    s_scr[PAGE_PLAYLIST] = scr;

    mk_label(scr, "\xe6\xad\x8c\xe6\x9b\xb2\xe5\x88\x97\xe8\xa1\xa8", cn_font(16), C_PRIMARY,
             LV_ALIGN_TOP_MID, 0, 8);

    // Scrollable song list
    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, SCR_W - 10, SCR_H - 40);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_bg_color(list, C_BG, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    add_focus(list);

    mk_label(scr, "SET:\xe8\xbf\x94\xe5\x9b\x9e", cn_font(14), C_DIM, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// =====================================================================
//  Page: PLAYING (美化播放界面)
// =====================================================================
static void create_playing_page() {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    s_scr[PAGE_PLAYING] = scr;

    // Cover image (supports network JPEG via TJPGD)
    s_cover_img = lv_image_create(scr);
    lv_obj_set_size(s_cover_img, 180, 140);
    lv_obj_align(s_cover_img, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_radius(s_cover_img, 12, 0);
    lv_obj_set_style_clip_corner(s_cover_img, true, 0);
    lv_obj_set_style_bg_color(s_cover_img, C_SURFACE, 0);
    lv_image_set_inner_align(s_cover_img, LV_IMAGE_ALIGN_CENTER);

    // Song title
    s_lbl_p_title = lv_label_create(scr);
    lv_label_set_text(s_lbl_p_title, "");
    lv_obj_set_style_text_font(s_lbl_p_title, cn_font(16), 0);
    lv_obj_set_style_text_color(s_lbl_p_title, C_TEXT, 0);
    lv_obj_align(s_lbl_p_title, LV_ALIGN_TOP_MID, 0, 160);
    lv_label_set_long_mode(s_lbl_p_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_lbl_p_title, 220);

    // Artist
    s_lbl_p_artist = lv_label_create(scr);
    lv_label_set_text(s_lbl_p_artist, "");
    lv_obj_set_style_text_font(s_lbl_p_artist, cn_font(14), 0);
    lv_obj_set_style_text_color(s_lbl_p_artist, C_TEXT_SEC, 0);
    lv_obj_align(s_lbl_p_artist, LV_ALIGN_TOP_MID, 0, 184);

    // Progress bar
    s_bar_progress = lv_bar_create(scr);
    lv_obj_set_size(s_bar_progress, 200, 6);
    lv_obj_align(s_bar_progress, LV_ALIGN_TOP_MID, 0, 210);
    lv_obj_set_style_bg_color(s_bar_progress, C_SURFACE2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_progress, C_PRIMARY, LV_PART_INDICATOR);
    lv_bar_set_value(s_bar_progress, 0, LV_ANIM_OFF);

    // Time
    s_lbl_time = mk_label(scr, "0:00/0:00", &lv_font_montserrat_14, C_DIM,
                           LV_ALIGN_TOP_MID, 0, 222);

    // Control buttons
    lv_obj_t *ctrl = lv_obj_create(scr);
    lv_obj_set_size(ctrl, 200, 40);
    lv_obj_align(ctrl, LV_ALIGN_TOP_MID, 0, 248);
    lv_obj_set_style_bg_opa(ctrl, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(ctrl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *bp = lv_button_create(ctrl);
    lv_obj_set_size(bp, 50, 36);
    lv_obj_set_style_bg_color(bp, C_SURFACE, 0);
    mk_label(bp, "<<", &lv_font_montserrat_16, C_TEXT, LV_ALIGN_CENTER, 0, 0);
    add_focus(bp);

    lv_obj_t *bpl = lv_button_create(ctrl);
    lv_obj_set_size(bpl, 60, 36);
    lv_obj_set_style_bg_color(bpl, C_PRIMARY, 0);
    s_lbl_play_icon = mk_label(bpl, ">", &lv_font_montserrat_18, C_TEXT, LV_ALIGN_CENTER, 0, 0);
    add_focus(bpl);

    lv_obj_t *bn = lv_button_create(ctrl);
    lv_obj_set_size(bn, 50, 36);
    lv_obj_set_style_bg_color(bn, C_SURFACE, 0);
    mk_label(bn, ">>", &lv_font_montserrat_16, C_TEXT, LV_ALIGN_CENTER, 0, 0);
    add_focus(bn);

    mk_label(scr, "UP:\xe6\xad\x8c\xe8\xaf\x8d SET:\xe8\xbf\x94\xe5\x9b\x9e",
             cn_font(14), C_DIM, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// =====================================================================
//  Page: LYRICS
// =====================================================================
static void create_lyrics_page() {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    s_scr[PAGE_LYRICS] = scr;

    mk_label(scr, "\xe6\xad\x8c\xe8\xaf\x8d", cn_font(18), C_PRIMARY, LV_ALIGN_TOP_MID, 0, 10);

    s_lbl_lyr_prev = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lbl_lyr_prev, cn_font(14), 0);
    lv_obj_set_style_text_color(s_lbl_lyr_prev, C_DIM, 0);
    lv_obj_align(s_lbl_lyr_prev, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_width(s_lbl_lyr_prev, 220);
    lv_label_set_long_mode(s_lbl_lyr_prev, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_lbl_lyr_prev, LV_TEXT_ALIGN_CENTER, 0);

    s_lbl_lyr_cur = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lbl_lyr_cur, cn_font(16), 0);
    lv_obj_set_style_text_color(s_lbl_lyr_cur, C_PRIMARY, 0);
    lv_obj_align(s_lbl_lyr_cur, LV_ALIGN_TOP_MID, 0, 130);
    lv_obj_set_width(s_lbl_lyr_cur, 220);
    lv_label_set_long_mode(s_lbl_lyr_cur, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_lbl_lyr_cur, LV_TEXT_ALIGN_CENTER, 0);

    s_lbl_lyr_next = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lbl_lyr_next, cn_font(14), 0);
    lv_obj_set_style_text_color(s_lbl_lyr_next, C_DIM, 0);
    lv_obj_align(s_lbl_lyr_next, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_set_width(s_lbl_lyr_next, 220);
    lv_label_set_long_mode(s_lbl_lyr_next, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_lbl_lyr_next, LV_TEXT_ALIGN_CENTER, 0);

    mk_label(scr, "SET:\xe8\xbf\x94\xe5\x9b\x9e", cn_font(14), C_DIM, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// =====================================================================
//  Page: SETTINGS
// =====================================================================
static void create_settings_page() {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    s_scr[PAGE_SETTINGS] = scr;

    mk_label(scr, "\xe8\xae\xbe\xe7\xbd\xae", cn_font(18), C_PRIMARY, LV_ALIGN_TOP_MID, 0, 10);

    for (int i = 0; i < 6; i++) {
        s_set_items[i] = lv_obj_create(scr);
        lv_obj_set_size(s_set_items[i], 220, 36);
        lv_obj_align(s_set_items[i], LV_ALIGN_TOP_MID, 0, 42 + i * 40);
        lv_obj_set_style_bg_color(s_set_items[i], C_SURFACE, 0);
        lv_obj_set_style_radius(s_set_items[i], 8, 0);
        lv_obj_set_style_border_width(s_set_items[i], 0, 0);
        lv_obj_set_style_pad_all(s_set_items[i], 0, 0);
        lv_obj_clear_flag(s_set_items[i], LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(s_set_items[i]);
        lv_label_set_text(lbl, s_set_labels[i]);
        lv_obj_set_style_text_font(lbl, cn_font(14), 0);
        lv_obj_set_style_text_color(lbl, C_TEXT, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 15, 0);
        add_focus(s_set_items[i]);
    }

    mk_label(scr, "SET:\xe8\xbf\x94\xe5\x9b\x9e", cn_font(14), C_DIM, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// =====================================================================
//  Page: WIFI
// =====================================================================
static void create_wifi_page() {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    s_scr[PAGE_WIFI] = scr;

    mk_label(scr, "WiFi\xe8\xae\xbe\xe7\xbd\xae", cn_font(18), C_PRIMARY, LV_ALIGN_TOP_MID, 0, 10);

    mk_label(scr, "SSID:", &lv_font_montserrat_14, C_TEXT_SEC, LV_ALIGN_TOP_LEFT, 15, 42);
    s_ta_ssid = lv_textarea_create(scr);
    lv_obj_set_size(s_ta_ssid, 210, 36);
    lv_obj_align(s_ta_ssid, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_color(s_ta_ssid, C_SURFACE, 0);
    lv_obj_set_style_text_color(s_ta_ssid, C_TEXT, 0);
    lv_textarea_set_one_line(s_ta_ssid, true);
    add_focus(s_ta_ssid);

    mk_label(scr, "\xe5\xaf\x86\xe7\xa0\x81:", cn_font(14), C_TEXT_SEC, LV_ALIGN_TOP_LEFT, 15, 105);
    s_ta_pass = lv_textarea_create(scr);
    lv_obj_set_size(s_ta_pass, 210, 36);
    lv_obj_align(s_ta_pass, LV_ALIGN_TOP_MID, 0, 123);
    lv_obj_set_style_bg_color(s_ta_pass, C_SURFACE, 0);
    lv_obj_set_style_text_color(s_ta_pass, C_TEXT, 0);
    lv_textarea_set_one_line(s_ta_pass, true);
    lv_textarea_set_password_mode(s_ta_pass, true);
    add_focus(s_ta_pass);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 120, 36);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 178);
    lv_obj_set_style_bg_color(btn, C_PRIMARY, 0);
    mk_label(btn, "\xe8\xbf\x9e\xe6\x8e\xa5", cn_font(14), C_TEXT, LV_ALIGN_CENTER, 0, 0);
    add_focus(btn);

    s_lbl_wifi_st = mk_label(scr, "", &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_MID, 0, 225);

    mk_label(scr, "SET:\xe8\xbf\x94\xe5\x9b\x9e", cn_font(14), C_DIM, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// =====================================================================
//  Public API
// =====================================================================
void ui_init(void) {
    s_page = PAGE_MAIN;
    s_focus_idx = 0;
    s_group = lv_group_get_default();

    // Create all pages
    create_main_page();
    create_search_page();
    create_playlist_page();
    create_playing_page();
    create_lyrics_page();
    create_settings_page();
    create_wifi_page();

    // Toast on top layer
    s_toast = lv_label_create(lv_layer_top());
    lv_obj_set_style_bg_color(s_toast, C_SURFACE2, 0);
    lv_obj_set_style_text_color(s_toast, C_TEXT, 0);
    lv_obj_set_style_text_font(s_toast, cn_font(14), 0);
    lv_obj_set_style_pad_all(s_toast, 8, 0);
    lv_obj_set_style_radius(s_toast, 8, 0);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    // Show main page
    lv_scr_load(s_scr[PAGE_MAIN]);
    Serial.println("[UI] All pages created");
}

void ui_navigate(PageId page) {
    if (page >= PAGE_COUNT) return;
    s_page = page;
    s_focus_idx = 0;

    // Remove all objects from group and re-add for new page
    lv_group_remove_all_objs(s_group);

    switch (page) {
        case PAGE_MAIN:
            lv_scr_load(s_scr[PAGE_MAIN]);
            add_focus(s_rec_list);
            add_focus(s_new_list);
            s_focus_max = 2;
            break;
        case PAGE_SEARCH:
            lv_scr_load(s_scr[PAGE_SEARCH]);
            lv_textarea_set_text(s_ta_search, "");
            lv_keyboard_set_textarea(s_kb, s_ta_search);
            lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
            add_focus(s_ta_search);
            add_focus(s_search_results);
            s_focus_max = 2;
            break;
        case PAGE_PLAYLIST:
            lv_scr_load(s_scr[PAGE_PLAYLIST]);
            s_focus_max = 1;
            break;
        case PAGE_PLAYING:
            lv_scr_load(s_scr[PAGE_PLAYING]);
            s_focus_max = 3;
            break;
        case PAGE_LYRICS:
            lv_scr_load(s_scr[PAGE_LYRICS]);
            s_focus_max = 0;
            break;
        case PAGE_SETTINGS:
            lv_scr_load(s_scr[PAGE_SETTINGS]);
            for (int i = 0; i < 6; i++) add_focus(s_set_items[i]);
            s_focus_max = 6;
            break;
        case PAGE_WIFI:
            lv_scr_load(s_scr[PAGE_WIFI]);
            add_focus(s_ta_ssid);
            add_focus(s_ta_pass);
            s_focus_max = 3;
            break;
    }
}

PageId ui_current_page(void) { return s_page; }

void ui_update_song(const cfg_music *music) {
    if (!music) return;
    lv_label_set_text(s_lbl_p_title, music->song_name);
    // artist would need to be added to cfg_music
}

void ui_update_progress(int pos_ms, int dur_ms) {
    if (dur_ms <= 0) return;
    int pct = (pos_ms * 100) / dur_ms;
    lv_bar_set_value(s_bar_progress, pct, LV_ANIM_OFF);
    char buf[20];
    snprintf(buf, sizeof(buf), "%d:%02d/%d:%02d",
             pos_ms/60000, (pos_ms/1000)%60, dur_ms/60000, (dur_ms/1000)%60);
    lv_label_set_text(s_lbl_time, buf);
}

void ui_update_lyrics(const char *prev, const char *cur, const char *next) {
    lv_label_set_text(s_lbl_lyr_prev, prev ? prev : "");
    lv_label_set_text(s_lbl_lyr_cur, cur ? cur : "\xe6\x97\xa0\xe6\xad\x8c\xe8\xaf\x8d");
    lv_label_set_text(s_lbl_lyr_next, next ? next : "");
}

void ui_update_cover(const char *url) {
    if (s_cover_img && url && strlen(url) > 0) {
        // LVGL v9 can load network images if FS and decoder are configured
        // For now, set as source - TJPGD will decode JPEG
        lv_image_set_src(s_cover_img, url);
    }
}

void ui_show_toast(const char *msg) {
    lv_label_set_text(s_toast, msg);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -40);
}

void ui_set_wifi_status(bool connected) {
    s_wifi_ok = connected;
    if (s_lbl_wifi) {
        lv_label_set_text(s_lbl_wifi, connected ? "[W]" : "[--]");
        lv_obj_set_style_text_color(s_lbl_wifi, connected ? C_SUCCESS : C_DIM, 0);
    }
}

void ui_focus_move(int delta) {
    if (s_focus_max <= 0) return;
    s_focus_idx += delta;
    if (s_focus_idx < 0) s_focus_idx = s_focus_max - 1;
    if (s_focus_idx >= s_focus_max) s_focus_idx = 0;
}

int ui_focus_get(void) { return s_focus_idx; }

void ui_update_recommend_list(void) { /* TODO: populate from API */ }
void ui_update_search_results(void) { /* TODO: populate from API */ }
void ui_update_playlist(void) { /* TODO: populate from API */ }
