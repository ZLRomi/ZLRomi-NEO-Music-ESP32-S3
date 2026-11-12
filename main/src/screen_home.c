/*
 * screen_home.c —— 首页（横屏 320x240）
 *
 * 4 列 x 2 行焦点网格：上排 4 个推荐歌单（带封面），下排 4 个快捷入口。
 * 方向键移动焦点、OK 打开、SET 由框架统一处理。
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

#define CARD_W   72
#define CARD_H   84
#define CARD_X0  6
#define CARD_DX  78
#define CARD_Y0  40
#define CARD_DY  92

static focus_t s_focus;
static EXT_RAM_BSS_ATTR cover_t s_covers[4];
static bool    s_cover_loaded;

static void home_enter(void *arg)
{
    (void)arg;
    focus_init(&s_focus, 4, 2);
    s_cover_loaded = false;
}

static void ensure_covers(void)
{
    if (s_cover_loaded) return;
    s_cover_loaded = true;
    for (int i = 0; i < 4; i++) {
        if (i < g_music.playlists.count) {
            cover_get(g_music.playlists.items[i].id,
                      g_music.playlists.items[i].cover, &s_covers[i]);
        } else {
            cover_get(-1 - i, NULL, &s_covers[i]);
        }
    }
}

static void draw_card(int col, int row, const char *label, const cover_t *cover,
                      icon_id_t icon, bool focused)
{
    int x = CARD_X0 + col * CARD_DX;
    int y = CARD_Y0 + row * CARD_DY;

    if (focused) {
        gfx_rect(x - 2, y - 2, x + CARD_W + 2, y + CARD_H + 2, g_theme.focus);
        gfx_frame(x - 2, y - 2, x + CARD_W + 2, y + CARD_H + 2, g_theme.focus_edge);
    } else {
        gfx_rect(x, y, x + CARD_W, y + CARD_H, g_theme.surface);
    }

    uint16_t fg = focused ? g_theme.accent : g_theme.text_dim;
    if (cover) {
        gfx_blit(x + (CARD_W - COVER_W) / 2, y + 2, COVER_W, COVER_H, cover->px);
    } else if (icon != ICON_COUNT) {
        gfx_icon(x + (CARD_W - 16) / 2, y + 20, icon, fg);
    }
    /* 歌单名用小字，卡片 72px 宽能放 4 个汉字 */
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
    gfx_text_small(x + (CARD_W - tw) / 2, y + COVER_H + 6, short_name,
                   focused ? g_theme.text : g_theme.text_dim,
                   focused ? g_theme.focus : g_theme.surface);
}

static void home_draw(void)
{
    gfx_fill(g_theme.bg);
    ensure_covers();

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

    for (int i = 0; i < 4; i++) {
        char label[SONG_NAME_LEN];
        if (i < g_music.playlists.count) {
            strncpy(label, g_music.playlists.items[i].name, sizeof(label) - 1);
            label[sizeof(label) - 1] = '\0';
        } else {
            strncpy(label, "推荐歌单", sizeof(label) - 1);
            label[sizeof(label) - 1] = '\0';
        }
        draw_card(i, 0, label, &s_covers[i], ICON_COUNT,
                  focus_row(&s_focus) == 0 && focus_col(&s_focus) == i);
    }

    static const char *quick[4] = { "每日新歌", "搜索歌曲", "设置菜单", "正在播放" };
    static const icon_id_t qicons[4] = { ICON_LIST, ICON_SEARCH, ICON_GEAR, ICON_PLAY };
    for (int i = 0; i < 4; i++) {
        draw_card(i, 1, quick[i], NULL, qicons[i],
                  focus_row(&s_focus) == 1 && focus_col(&s_focus) == i);
    }
}

static bool home_event(input_event_t ev)
{
    if (focus_move(&s_focus, ev)) return true;

    if (ev == EV_OK) {
        int idx = s_focus.index;
        if (idx < 4) {
            if (idx < g_music.playlists.count) {
                if (!music_load_playlist(idx)) {
                    ui_toast("歌单加载失败，请检查网络");
                    return true;
                }
            } else {
                ui_toast("该歌单暂不可用");
                return true;
            }
            ui_goto(SCR_SONGS, NULL);
            return true;
        }
        switch (idx - 4) {
        case 0:
            if (!music_load_daily()) ui_toast("每日新歌加载失败");
            else ui_goto(SCR_SONGS, NULL);
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
