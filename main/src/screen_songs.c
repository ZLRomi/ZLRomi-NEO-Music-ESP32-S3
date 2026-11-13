/*
 * screen_songs.c —— 歌曲列表（横屏 320x240）
 *
 * 每行 32x32 封面缩略图 + 中文歌名 + 歌手；焦点行整行高亮并带左侧琥珀竖线。
 * 封面走 cover.c 的异步管线：draw 时 cover_peek() 只查缓存（未命中顺带登记
 * 后台下载，立即返回画占位音符），下一帧真图到了自动替换 —— 按键永不阻塞。
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "cover.h"
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
/* 封面缩略图 28x28（缓存里是 96x96，gfx_blit_scaled 就近缩放） */
#define THUMB     28
/* 封面占的横条宽度：图 + 右侧留白 */
#define ART       (THUMB + 6)

/* 本行封面拷贝缓冲（PSRAM：96x96x2=18KB，内部 RAM 有一就挤掉 TLS 握手） */
static EXT_RAM_BSS_ATTR cover_t s_thumb;

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

/**
 * 画一行的 28x28 封面。
 * cover_peek 未命中时只画音符占位并把下载排进后台队列（永不阻塞）；
 * 封面就绪后 cover_task 会调 ui_redraw()，下一帧这里自然贴上真图。
 */
static void draw_row_art(song_t *s, int x, int y, bool focused)
{
    bool hit = false;
    if (s->cover[0]) {
        hit = cover_peek(s->id, s->cover, &s_thumb);
    }
    if (hit) {
        gfx_blit_scaled(x, y, THUMB, THUMB, s_thumb.px, COVER_W, COVER_H);
    } else {
        gfx_icon(x + (THUMB - 16) / 2, y + (THUMB - 16) / 2, ICON_NOTE,
                 focused ? g_theme.accent : g_theme.accent_dim);
    }
}

static void songs_draw(void)
{
    gfx_fill(g_theme.bg);
    ensure_visible();

    char title[96];
    snprintf(title, sizeof(title), "%s  第 %d/%d 页  %d 首",
             g_music.list_title, g_music.page + 1,
             g_music.page_count > 0 ? g_music.page_count : 0,
             g_music.songs.count);
    gfx_text_small(8, 4, title, g_theme.accent, g_theme.bg);
    /* 标题右侧给状态留位（正在翻页时显示提示） */
    if (music_page_busy()) {
        gfx_text_small(GFX_W - 96, 4, "加载中…", g_theme.text_dim, g_theme.bg);
    }
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

        draw_row_art(s, ROW_X + 4, y + 4, focused);

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

    /* 底部翻页栏：左右键翻页（上一页/下一页），分页加载的入口就在这 */
    {
        int y = GFX_H - 26;
        gfx_hline(8, GFX_W - 8, y - 4, g_theme.line);
        char pg[96];
        if (g_music.page_count > 0) {
            snprintf(pg, sizeof(pg), "第 %d/%d 页", g_music.page + 1, g_music.page_count);
        } else {
            snprintf(pg, sizeof(pg), "第 %d 页", g_music.page + 1);
        }
        gfx_text_center_small(y, pg, g_theme.text_dim, g_theme.bg);
        gfx_text_small(10, y, "左键 上一页", g_theme.text_dim, g_theme.bg);
        gfx_text_small(GFX_W - 106, y, "右键 下一页", g_theme.text_dim, g_theme.bg);
    }
}

static void songs_tick(void)
{
    ensure_visible();
}

static bool songs_event(input_event_t ev)
{
    if (focus_move(&s_focus, ev)) {
        ensure_visible();
        return true;
    }
    if (ev == EV_RIGHT || ev == EV_RIGHT_REPEAT) {
        if (music_page_busy()) {
            ui_toast("正在加载…");
        } else if (!music_next_page()) {
            ui_toast(g_music.page_count > 0 ? "已经是最后一页" : "翻页失败");
        }
        return true;
    }
    if (ev == EV_LEFT || ev == EV_LEFT_REPEAT) {
        if (music_page_busy()) {
            ui_toast("正在加载…");
        } else if (!music_prev_page()) {
            ui_toast("已经是第一页");
        }
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
