/*
 * screen_search.c —— 中文搜索（横屏 320x240）
 *
 * 10x4 焦点软键盘：方向键在键格间移动，OK 按下当前格，SET 返回。
 * 第 4 行前 8 格是拼音候选汉字，本身也是可聚焦格子，OK 直接上屏。
 * 没有任何依赖触摸的操作。
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "music_state.h"
#include "pinyin.h"
#include "theme.h"

#define KB_X0     5
#define KB_Y0     40
#define CELL_W    30
#define CELL_H    42
#define CELL_GAP  1

static focus_t  s_focus;
static pinyin_t s_py;

static void search_enter(void *arg)
{
    (void)arg;
    focus_init(&s_focus, PY_COLS, PY_ROWS);
    s_focus.index = 0;
    pinyin_reset(&s_py);
}

static void draw_cell(int index, bool focused)
{
    int col = index % PY_COLS;
    int row = index / PY_COLS;
    int x = KB_X0 + col * (CELL_W + CELL_GAP);
    int y = KB_Y0 + row * (CELL_H + CELL_GAP);

    uint16_t bg = focused ? g_theme.focus : g_theme.surface;
    if (pinyin_key_type(index) == PK_CANDIDATE && s_py.cand_count > 0) {
        bg = focused ? g_theme.focus : g_theme.surface_hi;
    }
    gfx_rect(x, y, x + CELL_W, y + CELL_H, bg);
    if (focused) gfx_frame(x, y, x + CELL_W, y + CELL_H, g_theme.focus_edge);

    char label[16];
    pinyin_key_label(index, &s_py, label, sizeof(label));
    if (label[0]) {
        int tw = gfx_text_width_small(label);
        int tx = x + (CELL_W - tw) / 2;
        if (tx < x) tx = x;
        gfx_text_small(tx, y + (CELL_H - 16) / 2, label,
                       focused ? g_theme.accent : g_theme.text, bg);
    }
}

static void search_draw(void)
{
    gfx_fill(g_theme.bg);

    /* 输入区：已上屏中文 + 正在拼的拼音 + 光标 */
    char line[96];
    snprintf(line, sizeof(line), "%s%s_", s_py.text, s_py.input);
    gfx_text_small(8, 4, line, g_theme.text, g_theme.bg);
    gfx_hline(8, GFX_W - 8, 26, g_theme.line);

    for (int i = 0; i < PY_KEYS; i++) {
        draw_cell(i, s_focus.index == i);
    }
    gfx_text_center_small(216, "方向键移动  OK 输入  SET 返回", g_theme.text_dim, g_theme.bg);
}

static void search_tick(void)
{
    if (!s_py.submit) return;
    s_py.submit = false;
    const char *kw = pinyin_text(&s_py);
    if (*kw) {
        if (music_load_search(kw)) {
            ui_goto(SCR_SONGS, NULL);
        } else {
            ui_toast("没有找到歌曲，换个关键词试试");
            ui_redraw();
        }
    }
}

static bool search_event(input_event_t ev)
{
    if (focus_move(&s_focus, ev)) return true;
    if (ev == EV_OK) {
        pinyin_activate(&s_py, s_focus.index);
        return true;
    }
    if (ev == EV_OK_LONG) {
        pinyin_reset(&s_py);
        return true;
    }
    return false;
}

const screen_ops_t g_screen_search = {
    .enter    = search_enter,
    .draw     = search_draw,
    .on_event = search_event,
    .tick     = search_tick,
};
