/*
 * screen_settings.c —— 设置（横屏 320x240）
 *
 * 全部条目用 7 键操作：上下移动焦点，左右/OK 调整，SET 返回。
 * WiFi 一行显示真实连接状态与 IP，没连上绝不画信号格。
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "esp_system.h"
#include "gfx.h"
#include "music_state.h"
#include "player.h"
#include "settings.h"
#include "theme.h"
#include "wifi_mgr.h"

#define ITEM_COUNT  7
#define ROW_Y0      38
#define ROW_H       24

static focus_t s_focus;

static void settings_enter(void *arg)
{
    (void)arg;
    focus_init(&s_focus, 1, ITEM_COUNT);
}

static void item_value(int i, char *buf, size_t cap)
{
    buf[0] = '\0';
    switch (i) {
    case 0: {
        wifi_state_t st = wifi_mgr_state();
        if (st == WIFI_CONNECTED) {
            snprintf(buf, cap, "%s %s", wifi_mgr_state_text(), wifi_mgr_ip());
        } else if (st == WIFI_AP) {
            snprintf(buf, cap, "热点 %s", wifi_mgr_ap_ssid());
        } else {
            snprintf(buf, cap, "%s", wifi_mgr_state_text());
        }
        break;
    }
    case 1: snprintf(buf, cap, "%d", g_cfg.volume); break;
    case 2: {
        static const char *q[3] = { "标准", "较高", "无损" };
        snprintf(buf, cap, "%s", q[g_cfg.quality]);
        break;
    }
    case 3: snprintf(buf, cap, "%d", g_cfg.brightness); break;
    case 4: snprintf(buf, cap, "%s", g_cfg.rotation == 0 ? "横屏" : "横屏翻转"); break;
    case 5: snprintf(buf, cap, "%s", music_mode_text()); break;
    case 6: snprintf(buf, cap, "版本 %s", FW_VERSION); break;
    }
}

static void settings_draw(void)
{
    gfx_fill(g_theme.bg);
    gfx_text(10, 2, "设置", g_theme.accent, g_theme.bg);
    gfx_hline(10, GFX_W - 10, 30, g_theme.line);

    static const char *labels[ITEM_COUNT] = {
        "WiFi 网络", "音量", "音质", "屏幕亮度", "屏幕方向", "播放模式", "关于本机"
    };

    for (int i = 0; i < ITEM_COUNT; i++) {
        int y = ROW_Y0 + i * ROW_H;
        bool focused = (s_focus.index == i);
        gfx_focus_row(8, y, GFX_W - 16, ROW_H - 2, focused,
                      (i % 2 == 0) ? g_theme.bg : g_theme.surface);
        uint16_t bg = focused ? g_theme.focus
                              : ((i % 2 == 0) ? g_theme.bg : g_theme.surface);
        gfx_text(14, y + 1, labels[i], g_theme.text, bg);

        char val[64];
        item_value(i, val, sizeof(val));
        int vw = gfx_text_width(val);
        int vx = GFX_W - 14 - vw;
        if (vx < 130) vx = 130;
        gfx_text(vx, y + 1, val, g_theme.text_dim, bg);
    }

    gfx_text_center(212, "上下选择  左右调整  OK 切换  SET 返回",
                    g_theme.text_dim, g_theme.bg);
}

static void adjust(int i, int delta)
{
    switch (i) {
    case 0:
        ui_toast("长按 OK 重置配网，重启后开热点");
        break;
    case 1: {
        int v = (int)g_cfg.volume + delta * 5;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        g_cfg.volume = (uint8_t)v;
        player_set_volume(v);
        settings_save();
        break;
    }
    case 2:
        g_cfg.quality = (uint8_t)((g_cfg.quality + 3 + delta % 3) % 3);
        settings_save();
        break;
    case 3: {
        int v = (int)g_cfg.brightness + delta * 10;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        g_cfg.brightness = (uint8_t)v;
        gfx_brightness(v);
        settings_save();
        break;
    }
    case 4:
        g_cfg.rotation = (uint8_t)((g_cfg.rotation + 1) % 2);   /* 只保留两种横屏 */
        gfx_set_rotation(g_cfg.rotation);
        settings_save();
        break;
    case 5:
        music_set_mode((g_music.mode + 1) % 3);
        break;
    case 6:
        ui_toast("HW-678 / ESP32-S3 / PCM5102A");
        break;
    }
}

static bool settings_event(input_event_t ev)
{
    if (ev == EV_UP || ev == EV_UP_REPEAT || ev == EV_DOWN || ev == EV_DOWN_REPEAT) {
        focus_move(&s_focus, ev);
        return true;
    }
    if (ev == EV_LEFT || ev == EV_LEFT_REPEAT) {
        adjust(s_focus.index, -1);
        return true;
    }
    if (ev == EV_RIGHT || ev == EV_RIGHT_REPEAT || ev == EV_OK) {
        adjust(s_focus.index, 1);
        return true;
    }
    if (ev == EV_OK_LONG && s_focus.index == 0) {
        /* 真·重新配网：清掉保存的 SSID，重启后 wifi_mgr 会开 AP */
        g_cfg.wifi_ssid[0] = '\0';
        g_cfg.wifi_pass[0] = '\0';
        settings_save();
        ui_toast("正在重启进入配网模式");
        esp_restart();
        return true;
    }
    return false;
}

const screen_ops_t g_screen_settings = {
    .enter    = settings_enter,
    .draw     = settings_draw,
    .on_event = settings_event,
    .tick     = NULL,
};
