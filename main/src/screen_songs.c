/*
 * screen_songs.c —— 歌曲列表（横屏 320x240）
 *
 * 每行都有封面 + 中文歌名 + 歌手；焦点行整行高亮并带左侧琥珀竖线。
 * 封面在 tick 里逐张下载，不阻塞按键响应。
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_timer.h"
#include "gfx.h"
#include "icon.h"
#include "music_state.h"
#include "netease.h"
#include "theme.h"

#define ROW_H     36
#define ROW_Y0    34
#define VISIBLE   5
#define ROW_X     6
#define ROW_W     (GFX_W - 12)
/* 文字左侧留出的图标位宽（列表不放封面，只放一个小音符） */
#define ART       28

static focus_t s_focus;
static int     s_top;

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
}

static void ensure_visible(void)
{
    int idx = s_focus.index;
    if (idx < s_top) s_top = idx;
    if (idx >= s_top + VISIBLE) s_top = idx - VISIBLE + 1;
    if (s_top < 0) s_top = 0;
}

/** 歌名太长就横向滚动（只滚焦点那一行，避免满屏都在动） */
static void draw_scroll_text(int x, int y, int maxw, const char *s,
                             uint16_t fg, uint16_t bg)
{
    int tw = gfx_text_width_small(s);
    if (tw <= maxw) {
        gfx_text_small(x, y, s, fg, bg);
        return;
    }
    int64_t now = esp_timer_get_time() / 1000;
    int  span  = tw - maxw + 20;           /* 需要滑动的距离 */
    int  cycle = span / 20 + 3;            /* 每秒 20px 的格数 */
    int  phase = (int)(now % (cycle * 2));
    int  off   = (phase < cycle) ? phase * 20 : (cycle * 2 - phase) * 20;
    if (off > span) off = span;

    gfx_text_small(x - off, y, s, fg, bg);
    gfx_text_small(x - off + tw + 20, y, s, fg, bg);
}

static void songs_draw(void)
{
    gfx_fill(g_theme.bg);
    ensure_visible();

    char title[96];
    snprintf(title, sizeof(title), "%s  %d 首",
             g_music.list_title, g_music.songs.count);
    gfx_text_small(8, 4, title, g_theme.accent, g_theme.bg);
    gfx_hline(8, GFX_W - 8, 26, g_theme.line);

    if (g_music.songs.count == 0) {
        gfx_text_center_small(110, "列表为空，请检查网络", g_theme.text_dim, g_theme.bg);
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

        /* 左侧一个小音符当序号图标。列表不下载封面：
           一张就要下 20KB 还要 JPEG 解码，上下滚会明显卡（用户明确要求）。 */
        gfx_icon(ROW_X + 6, y + 8, ICON_NOTE,
                 focused ? g_theme.accent : g_theme.accent_dim);

        uint16_t bg = focused ? g_theme.focus
                              : ((v % 2 == 0) ? g_theme.bg : g_theme.surface);
        int tx = ROW_X + ART + 8;
        int tw = ROW_W - ART - 12;

        if (focused) {
            draw_scroll_text(tx, y + 1, tw, s->name, g_theme.text, bg);
        } else {
            /* 非焦点行超长时截断 */
            char name[SONG_NAME_LEN];
            strncpy(name, s->name, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            while (gfx_text_width_small(name) > tw && strlen(name) > 3) {
                size_t n = strlen(name);
                size_t back = (name[n - 1] & 0x80) ? 3 : 1;
                name[n - back] = '\0';
            }
            gfx_text_small(tx, y + 1, name, g_theme.text, bg);
        }
        gfx_text_small(tx, y + 19, s->artist, g_theme.text_dim, bg);
    }
}

static void songs_tick(void)
{
    /* 列表不下载封面（用户明确要求：只在播放详情页要），所以这里没有活干。
       保留 tick 是为了以后加东西不用改 ops 表。 */
    ensure_visible();
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
