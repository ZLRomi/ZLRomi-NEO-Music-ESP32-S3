/*
 * screen_restart.c —— RST 长按 3 秒后的二次确认页
 *
 * 不提供「关机」；页面只做一件事：提示再长按 3 秒才真正重启。
 * 全程 7 键：焦点落在「取消返回」上，OK / SET 取消，再长按 RST 重启。
 */
#include "ui.h"

#include "esp_log.h"
#include "esp_system.h"
#include "gfx.h"
#include "icon.h"
#include "theme.h"

static const char *TAG = "restart";
static focus_t s_focus;

static void restart_enter(void *arg)
{
    (void)arg;
    focus_init(&s_focus, 1, 1);   /* 进入即重置焦点，保证可见 */
}

static void restart_draw(void)
{
    gfx_fill(g_theme.bg);

    gfx_icon(GFX_W / 2 - 8, 30, ICON_GEAR, g_theme.accent);
    gfx_text_center_small(56, "重启设备", g_theme.accent, g_theme.bg);
    gfx_hline(56, GFX_W - 56, 80, g_theme.line);

    gfx_text_center_small(100, "再长按 RST 键 3 秒", g_theme.text, g_theme.bg);
    gfx_text_center_small(122, "即可重新启动", g_theme.text, g_theme.bg);
    gfx_text_center_small(150, "按 SET 或 OK 取消", g_theme.text_dim, g_theme.bg);

    /* 唯一可聚焦元素，始终高亮，电视遥控风格 */
    int w = 200, x = (GFX_W - w) / 2, y = 196, h = 32;
    gfx_focus_row(x, y, w, h, true, g_theme.bg);
    gfx_text_center_small(y + 8, "取消返回", g_theme.text, g_theme.focus);
}

static bool restart_event(input_event_t ev)
{
    switch (ev) {
    case EV_RST_LONG:
        ESP_LOGW(TAG, "二次确认通过，重启");
        esp_restart();
        return true;                 /* 不会走到这里 */

    case EV_OK:
    case EV_RST:
        ui_back();
        return true;

    case EV_UP:
    case EV_UP_REPEAT:
    case EV_DOWN:
    case EV_DOWN_REPEAT:
    case EV_LEFT:
    case EV_LEFT_REPEAT:
    case EV_RIGHT:
    case EV_RIGHT_REPEAT:
        focus_move(&s_focus, ev);
        return true;                 /* 只有一个元素，焦点保持不动 */

    default:
        return false;
    }
}

const screen_ops_t g_screen_restart = {
    .enter    = restart_enter,
    .draw     = restart_draw,
    .on_event = restart_event,
    .tick     = NULL,
};
