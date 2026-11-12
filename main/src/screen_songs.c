/*
 * screen_songs.c —— 歌曲列表（横屏 320x240）
 *
 * 每行都有封面 + 中文歌名 + 歌手；焦点行整行高亮并带左侧琥珀竖线。
 * 封面在 tick 里逐张下载，不阻塞按键响应。
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "cover.h"
#include "gfx.h"
#include "music_state.h"
#include "netease.h"
#include "theme.h"

#define ROW_H     50
#define ROW_Y0    38
#define VISIBLE   4
#define ROW_X     8
#define ROW_W     (GFX_W - 16)
#define ICON      40

static focus_t s_focus;
static int     s_top;
static cover_t s_covers[VISIBLE];
static int64_t s_cover_ids[VISIBLE];
static bool    s_cover_ok[VISIBLE];

static void songs_enter(void *arg)
{
    (void)arg;
    int n = g_music.songs.count > 0 ? g_music.songs.count : 1;
    focus_init(&s_focus, 1, n);
    /* 进入列表时把焦点落在当前播放的曲目上 */
    if (g_music.current >= 0 && g_music.current < n) {
        s_focus.index = g_music.current;
    }
    s_top = 0;
    for (int i = 0; i < VISIBLE; i++) {
        s_cover_ids[i] = 0;
        s_cover_ok[i] = false;
    }
}

static void ensure_visible(void)
{
    int idx = s_focus.index;
    if (idx < s_top) s_top = idx;
    if (idx >= s_top + VISIBLE) s_top = idx - VISIBLE + 1;
    if (s_top < 0) s_top = 0;
}

static void draw_placeholder(int x, int y)
{
    gfx_rect(x, y, x + ICON, y + ICON, g_theme.surface_hi);
    gfx_frame(x, y, x + ICON, y + ICON, g_theme.line);
    gfx_text(x + 12, y + 8, "曲", g_theme.accent_dim, g_theme.surface_hi);
}

static void songs_draw(void)
{
    gfx_fill(g_theme.bg);
    ensure_visible();

    char title[96];
    snprintf(title, sizeof(title), "%s  %d 首",
             g_music.list_title, g_music.songs.count);
    gfx_text(10, 4, title, g_theme.accent, g_theme.bg);
    gfx_hline(10, GFX_W - 10, 34, g_theme.line);

    if (g_music.songs.count == 0) {
        gfx_text_center(110, "列表为空，请检查网络", g_theme.text_dim, g_theme.bg);
        return;
    }

    for (int v = 0; v < VISIBLE; v++) {
        int idx = s_top + v;
        if (idx >= g_music.songs.count) break;
        song_t *s = &g_music.songs.items[idx];
        int y = ROW_Y0 + v * ROW_H;
        bool focused = (idx == s_focus.index);

        gfx_focus_row(ROW_X, y, ROW_W, ROW_H - 2, focused,
                      (v % 2 == 0) ? g_theme.bg : g_theme.surface);

        int cx = ROW_X + 5, cy = y + 5;
        bool have = false;
        for (int k = 0; k < VISIBLE; k++) {
            if (s_cover_ok[k] && s_cover_ids[k] == s->id) {
                gfx_blit_scaled(cx, cy, ICON, ICON,
                                s_covers[k].px, COVER_W, COVER_H);
                have = true;
                break;
            }
        }
        if (!have) draw_placeholder(cx, cy);

        uint16_t bg = focused ? g_theme.focus
                              : ((v % 2 == 0) ? g_theme.bg : g_theme.surface);
        /* 歌名过长时截断到一行 */
        char name[SONG_NAME_LEN];
        strncpy(name, s->name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        while (gfx_text_width(name) > ROW_W - ICON - 16 && strlen(name) > 3) {
            size_t n = strlen(name);
            size_t back = (name[n - 1] & 0x80) ? 3 : 1;
            name[n - back] = '\0';
        }
        gfx_text(ROW_X + ICON + 12, y + 1, name, g_theme.text, bg);
        gfx_text(ROW_X + ICON + 12, y + 25, s->artist, g_theme.text_dim, bg);
    }
}

/** 每帧只补一张封面，避免一次性发起多个 HTTPS 请求把按键卡住 */
static void songs_tick(void)
{
    ensure_visible();
    for (int v = 0; v < VISIBLE; v++) {
        int idx = s_top + v;
        if (idx >= g_music.songs.count) break;
        song_t *s = &g_music.songs.items[idx];

        bool already = false;
        for (int k = 0; k < VISIBLE; k++) {
            if (s_cover_ok[k] && s_cover_ids[k] == s->id) { already = true; break; }
        }
        if (already) continue;

        int slot = -1;
        for (int k = 0; k < VISIBLE; k++) {
            if (!s_cover_ok[k]) { slot = k; break; }
        }
        if (slot < 0) slot = v;

        cover_get(s->id, s->cover, &s_covers[slot]);
        s_cover_ids[slot] = s->id;
        s_cover_ok[slot] = true;
        ui_redraw();
        return;
    }
}

static bool songs_event(input_event_t ev)
{
    if (focus_move(&s_focus, ev)) {
        ensure_visible();
        return true;
    }
    if (ev == EV_OK) {
        if (g_music.songs.count == 0) return true;
        g_music.current = s_focus.index;
        if (!music_play_current()) ui_toast("播放地址获取失败");
        else ui_goto(SCR_PLAYER, NULL);
        return true;
    }
    if (ev == EV_OK_LONG) {
        ui_goto(SCR_PLAYER, NULL);
        return true;
    }
    return false;
}

const screen_ops_t g_screen_songs = {
    .enter    = songs_enter,
    .draw     = songs_draw,
    .on_event = songs_event,
    .tick     = songs_tick,
};
