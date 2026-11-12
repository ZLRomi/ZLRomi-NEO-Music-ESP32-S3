/*
 * screen_wifi.c —— 扫描周围 WiFi、选网络、输密码、存下来自动连接
 *
 * 两页：
 *   扫描页  列出周围热点（信号格 + 锁图标），上下选，OK 连
 *   密码页  ASCII 软键盘输入密码，OK 确认后保存并连接
 * 全程 7 键：方向键移动焦点，OK 确认，SET 返回。
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "icon.h"
#include "pinyin.h"
#include "theme.h"
#include "wifi_mgr.h"

typedef enum {
    WP_SCAN = 0,
    WP_PASS,
} wifi_page_t;

#define ROW_H     26
#define LIST_Y0   34
#define VISIBLE   6
#define KB_X0     5
#define KB_Y0     64

static wifi_page_t s_page;
static wifi_ap_t   s_aps[WIFI_SCAN_MAX];
static int         s_ap_count;
static bool        s_scanning;
static focus_t     s_focus;
static pinyin_t    s_kb;
static char        s_sel[33];

static void draw_signal(int x, int y, int rssi)
{
    int bars = (rssi > -60) ? 4 : (rssi > -70) ? 3 : (rssi > -80) ? 2 : 1;
    for (int i = 0; i < 4; i++) {
        int h = 3 + i * 3;
        uint16_t c = (i < bars) ? g_theme.accent : g_theme.line;
        gfx_rect(x + i * 5, y + 12 - h, x + i * 5 + 3, y + 12, c);
    }
}

static void wifi_enter(void *arg)
{
    (void)arg;
    s_page      = WP_SCAN;
    s_ap_count  = 0;
    s_scanning  = true;        /* 下一帧再扫，别卡住进入动作 */
    focus_init(&s_focus, 1, WIFI_SCAN_MAX);
    pinyin_set_mode(KB_ASCII);
}

static void draw_scan(void)
{
    gfx_fill(g_theme.bg);

    gfx_icon(8, 3, ICON_WIFI, g_theme.accent);
    gfx_text(28, 2, "选择网络", g_theme.accent, g_theme.bg);
    char st[24];
    if (s_scanning) {
        snprintf(st, sizeof(st), "扫描中");
    } else {
        snprintf(st, sizeof(st), "%d 个", s_ap_count);
    }
    int sw = gfx_text_width_small(st);
    gfx_text_small(GFX_W - sw - 8, 8, st, g_theme.text_dim, g_theme.bg);
    gfx_hline(8, GFX_W - 8, 28, g_theme.line);

    if (s_scanning) {
        gfx_text_center_small(110, "正在扫描周围网络…", g_theme.text_dim, g_theme.bg);
        return;
    }
    if (s_ap_count == 0) {
        gfx_text_center_small(110, "没有扫到网络", g_theme.text_dim, g_theme.bg);
        gfx_text_center_small(132, "长按 OK 重新扫描", g_theme.text_dim, g_theme.bg);
        return;
    }

    /* 焦点行尽量居中，列表跟着滚 */
    int top = s_focus.index - VISIBLE / 2;
    if (top < 0) top = 0;
    if (top > s_ap_count - VISIBLE) top = s_ap_count - VISIBLE;
    if (top < 0) top = 0;

    for (int v = 0; v < VISIBLE; v++) {
        int idx = top + v;
        if (idx >= s_ap_count) break;
        wifi_ap_t *ap = &s_aps[idx];
        int y = LIST_Y0 + v * ROW_H;
        bool focused = (idx == s_focus.index);

        gfx_focus_row(6, y, GFX_W - 12, ROW_H - 2, focused,
                      (v % 2 == 0) ? g_theme.bg : g_theme.surface);
        uint16_t bg = focused ? g_theme.focus
                              : ((v % 2 == 0) ? g_theme.bg : g_theme.surface);

        int tx = 12;
        if (ap->locked) {
            gfx_icon(tx, y + 5, ICON_LOCK, focused ? g_theme.accent : g_theme.text_dim);
            tx += 22;
        }
        /* 名字过长截断 */
        char name[33];
        strncpy(name, ap->ssid, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        while (gfx_text_width_small(name) > GFX_W - tx - 80 && strlen(name) > 3) {
            size_t n = strlen(name);
            size_t back = (name[n - 1] & 0x80) ? 3 : 1;
            name[n - back] = '\0';
        }
        gfx_text_small(tx, y + 5, name, g_theme.text, bg);
        draw_signal(GFX_W - 34, y + 5, ap->rssi);
    }

    gfx_text_center_small(216, "上下选择  OK 连接  长按 OK 重扫  SET 返回",
                          g_theme.text_dim, g_theme.bg);
}

static void draw_pass(void)
{
    gfx_fill(g_theme.bg);

    gfx_icon(8, 3, ICON_LOCK, g_theme.accent);
    gfx_text(28, 2, "输入密码", g_theme.accent, g_theme.bg);
    int nw = gfx_text_width_small(s_sel);
    gfx_text_small(GFX_W - nw - 8, 8, s_sel, g_theme.text_dim, g_theme.bg);
    gfx_hline(8, GFX_W - 8, 28, g_theme.line);

    /* 已输入的密码 */
    gfx_rect(8, 34, GFX_W - 8, 58, g_theme.surface);
    const char *pw = pinyin_text(&s_kb);
    gfx_text_small(14, 38, pw, g_theme.text, g_theme.surface);
    int pw_w = gfx_text_width_small(pw);
    gfx_rect(14 + pw_w + 2, 36, 15 + pw_w, 56, g_theme.accent);

    pinyin_draw_keyboard(&s_kb, s_focus.index, KB_X0, KB_Y0);
}

static void wifi_draw(void)
{
    if (s_page == WP_SCAN) {
        draw_scan();
    } else {
        draw_pass();
    }
}

static void do_connect(void)
{
    const char *pw = pinyin_text(&s_kb);
    wifi_mgr_save(s_sel, pw);
    wifi_mgr_connect(s_sel, pw);
    s_page = WP_SCAN;
    focus_init(&s_focus, 1, WIFI_SCAN_MAX);
    ui_toast("正在连接…");
}

static bool wifi_event(input_event_t ev)
{
    if (s_page == WP_SCAN) {
        if (focus_move(&s_focus, ev)) {
            return true;
        }
        if (ev == EV_OK) {
            if (s_ap_count == 0) {
                ui_toast("没有可连接的网络");
                return true;
            }
            wifi_ap_t *ap = &s_aps[s_focus.index];
            strncpy(s_sel, ap->ssid, sizeof(s_sel) - 1);
            s_sel[sizeof(s_sel) - 1] = '\0';
            if (!ap->locked) {
                /* 开放网络不用密码，直接连 */
                pinyin_reset(&s_kb);
                do_connect();
                return true;
            }
            pinyin_reset(&s_kb);
            focus_init(&s_focus, PY_COLS, PY_ROWS);
            s_page = WP_PASS;
            return true;
        }
        if (ev == EV_OK_LONG) {
            s_scanning = true;
            s_ap_count = 0;
            ui_redraw();
            return true;
        }
        return false;
    }

    /* 密码页 */
    if (focus_move(&s_focus, ev)) {
        return true;
    }
    if (ev == EV_OK) {
        pinyin_activate(&s_kb, s_focus.index);
        return true;
    }
    if (ev == EV_OK_LONG) {
        pinyin_reset(&s_kb);
        return true;
    }
    return false;
}

static void wifi_tick(void)
{
    if (s_page == WP_PASS && s_kb.submit) {
        s_kb.submit = false;
        do_connect();
        return;
    }
    if (s_scanning) {
        s_scanning = false;
        s_ap_count = wifi_mgr_scan(s_aps, WIFI_SCAN_MAX);
        focus_clamp(&s_focus, s_ap_count > 0 ? s_ap_count : 1);
        ui_redraw();
    }
}

const screen_ops_t g_screen_wifi = {
    .enter    = wifi_enter,
    .draw     = wifi_draw,
    .on_event = wifi_event,
    .tick     = wifi_tick,
};
