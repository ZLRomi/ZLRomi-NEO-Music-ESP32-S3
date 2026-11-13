/*
 * screen_home.c —— 首页（横屏 320x240）
 *
 * 4 列 x 2 行焦点网格：上排 4 个推荐歌单（带封面），下排 4 个快捷入口。
 * 方向键移动焦点、OK 打开、SET 由框架统一处理。
 * 封面走 cover.c 异步管线：cover_peek() 未命中只画占位图标并登记后台下载，
 * 下一帧自动替换 —— 列表滚动与按键永不等待网络。
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "cover.h"
#include "esp_attr.h"
#include "gfx.h"
#include "icon.h"
#include "music_state.h"
#include "netease.h"
#include "settings.h"
#include "theme.h"
#include "wifi_mgr.h"

/* 卡片布局：屏幕 320 宽，一行 4 张。
 * (320 - 2*4) / 4 = 78，留 2px 间隙后卡片宽 76。
 * 封面统一存 96x96，卡片里缩到 64 显示，下面留一行歌单名。 */
#define CARD_W   76
#define CARD_H   88
#define CARD_X0  4
#define CARD_DX  79
#define CARD_Y0  38
#define CARD_DY  96
#define ART_SIZE 64

/* 本卡片封面拷贝缓冲（PSRAM：96x96x2=18KB） */
static EXT_RAM_BSS_ATTR cover_t s_card_art;

static focus_t s_focus;

static void home_enter(void *arg)
{
    (void)arg;
    focus_init(&s_focus, 4, 2);
}

/**
 * 画一张卡片。
 * 上排歌单卡：歌单封面（cover_peek 未命中画占位图标，后台下载完自动换）。
 * 下排快捷卡：程序化几何图标，不涉及网络。
 */
static void draw_card(int col, int row, const char *label, icon_id_t icon,
                      bool focused, int playlist_index)
{
    int x = CARD_X0 + col * CARD_DX;
    int y = CARD_Y0 + row * CARD_DY;

    if (focused) {
        gfx_rect(x - 2, y - 2, x + CARD_W + 2, y + CARD_H + 2, g_theme.focus);
        gfx_frame(x - 2, y - 2, x + CARD_W + 2, y + CARD_H + 2, g_theme.focus_edge);
    } else {
        gfx_rect(x, y, x + CARD_W, y + CARD_H, g_theme.surface);
    }
    uint16_t bg = focused ? g_theme.focus : g_theme.surface;

    int ax = x + (CARD_W - ART_SIZE) / 2;
    int ay = y + 2;

    bool have_art = false;
    if (row == 0 && playlist_index >= 0 && playlist_index < g_music.playlists.count) {
        playlist_t *p = &g_music.playlists.items[playlist_index];
        if (p->cover[0]) {
            have_art = cover_peek(p->id, p->cover, &s_card_art);
        }
    }
    if (have_art) {
        gfx_blit_scaled(ax, ay, ART_SIZE, ART_SIZE, s_card_art.px, COVER_W, COVER_H);
    } else if (row == 0 && playlist_index >= 0) {
        /* 歌单卡占位：画一个列表图标占住封面位置 */
        gfx_icon(x + (CARD_W - 16) / 2, ay + (ART_SIZE - 16) / 2, ICON_LIST,
                 focused ? g_theme.accent : g_theme.text_dim);
    } else {
        gfx_icon(x + (CARD_W - 16) / 2, ay + (ART_SIZE - 16) / 2, icon,
                 focused ? g_theme.accent : g_theme.text_dim);
    }

    /* 歌单名用小字，卡片 76px 宽能放 4 个汉字 */
    char short_name[25];
    size_t bytes = 0, chars = 0;
    const char *p = label;
    while (*p && chars < 4) {
        size_t n = (*p & 0x80) ? 3 : 1;
        if (bytes + n >= sizeof(short_name)) break;
        memcpy(short_name + bytes, p, n);
        bytes += n;
        p += n;
        chars++;
    }
    short_name[bytes] = '\0';
    int tw = gfx_text_width_small(short_name);
    gfx_text_small(x + (CARD_W - tw) / 2, y + 70, short_name,
                   focused ? g_theme.text : g_theme.text_dim, bg);
}

static void home_draw(void)
{
    gfx_fill(g_theme.bg);

    /* 顶栏：标题 + 真实 WiFi 状态，没连上绝不显示信号格 */
    gfx_text(8, 2, "NEO 音乐", g_theme.accent, g_theme.bg);
    char net[64];
    wifi_state_t st = wifi_mgr_state();
    if (st == WIFI_CONNECTED) {
        snprintf(net, sizeof(net), "%s %ddBm", wifi_mgr_state_text(), wifi_mgr_rssi());
    } else {
        snprintf(net, sizeof(net), "%s", wifi_mgr_state_text());
    }
    int nw = gfx_text_width_small(net);
    uint16_t nc = (st == WIFI_CONNECTED) ? g_theme.ok : g_theme.text_dim;
    gfx_icon(GFX_W - nw - 26, 4, ICON_WIFI, nc);
    gfx_text_small(GFX_W - nw - 8, 6, net, nc, g_theme.bg);
    gfx_hline(8, GFX_W - 8, 32, g_theme.line);

    /* 上排：4 个歌单（带封面，异步替换） */
    for (int i = 0; i < 4; i++) {
        char label[SONG_NAME_LEN];
        const char *nm;
        if (i < g_music.playlists.count) {
            nm = g_music.playlists.items[i].name;
        } else {
            nm = "推荐歌单";
        }
        strncpy(label, nm, sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';
        draw_card(i, 0, label, ICON_LIST,
                  focus_row(&s_focus) == 0 && focus_col(&s_focus) == i,
                  i < g_music.playlists.count ? i : -1);
    }

    /* 下排：4 个快捷入口 */
    static const char *quick[4] = { "每日新歌", "搜索歌曲", "设置菜单", "正在播放" };
    static const icon_id_t qicons[4] = { ICON_LIST, ICON_SEARCH, ICON_GEAR, ICON_PLAY };
    for (int i = 0; i < 4; i++) {
        draw_card(i, 1, quick[i], qicons[i],
                  focus_row(&s_focus) == 1 && focus_col(&s_focus) == i, -1);
    }
}

static bool home_event(input_event_t ev)
{
    if (focus_move(&s_focus, ev)) return true;

    if (ev == EV_OK) {
        int idx = s_focus.index;
        if (idx < 4) {
            if (idx < g_music.playlists.count) {
                /* 进「正在加载」页，后台拉曲目，拉完自动跳列表 */
                ui_show_loading(idx);
            } else {
                ui_toast("该歌单暂不可用");
            }
            return true;
        }
        switch (idx - 4) {
        case 0:
            /* 每日新歌同样走加载页 */
            ui_show_loading(-1);
            break;
        case 1:
            ui_goto(SCR_SEARCH, NULL);
            break;
        case 2:
            ui_goto(SCR_SETTINGS, NULL);
            break;
        case 3:
            ui_goto(SCR_PLAYER, NULL);
            break;
        }
        return true;
    }

    if (ev == EV_OK_LONG) {
        ui_goto(SCR_SETTINGS, NULL);
        return true;
    }
    return false;
}

const screen_ops_t g_screen_home = {
    .enter    = home_enter,
    .draw     = home_draw,
    .on_event = home_event,
    .tick     = NULL,
};
