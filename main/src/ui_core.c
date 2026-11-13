#include "ui.h"

#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "gfx.h"
#include "settings.h"
#include "theme.h"
#include "wifi_mgr.h"

static const char *TAG = "ui";

#define SCREEN_STACK_MAX  8
#define IDLE_SLEEP_MS     60000
#define TOAST_MS          2200

static const screen_ops_t *s_ops[SCR_COUNT] = {
    [SCR_HOME]     = &g_screen_home,
    [SCR_SONGS]    = &g_screen_songs,
    [SCR_PLAYER]   = &g_screen_player,
    [SCR_SEARCH]   = &g_screen_search,
    [SCR_SETTINGS] = &g_screen_settings,
    [SCR_WIFI]      = &g_screen_wifi,
    [SCR_RESTART]  = &g_screen_restart,
};

typedef struct {
    screen_id_t id;
    void       *arg;
} stack_entry_t;

static stack_entry_t s_stack[SCREEN_STACK_MAX];
static int           s_sp;
static bool          s_dirty = true;
static bool          s_asleep;
static int64_t       s_last_act;
static int64_t       s_toast_at;
static char          s_toast[64];
static wifi_state_t  s_last_wifi = WIFI_OFF;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

void focus_init(focus_t *f, int cols, int rows)
{
    f->cols  = cols > 0 ? cols : 1;
    f->rows  = rows > 0 ? rows : 1;
    f->index = 0;
}

int focus_col(const focus_t *f)
{
    return f->index % f->cols;
}

int focus_row(const focus_t *f)
{
    return f->index / f->cols;
}

void focus_clamp(focus_t *f, int item_count)
{
    if (item_count <= 0) {
        f->index = 0;
        return;
    }
    if (f->index >= item_count) {
        f->index = item_count - 1;
    }
}

bool focus_move(focus_t *f, input_event_t ev)
{
    int c = f->index % f->cols;
    int r = f->index / f->cols;
    int old = f->index;

    switch (ev) {
    case EV_UP:
    case EV_UP_REPEAT:
        r = (r - 1 + f->rows) % f->rows;
        break;
    case EV_DOWN:
    case EV_DOWN_REPEAT:
        r = (r + 1) % f->rows;
        break;
    case EV_LEFT:
    case EV_LEFT_REPEAT:
        c = (c - 1 + f->cols) % f->cols;
        break;
    case EV_RIGHT:
    case EV_RIGHT_REPEAT:
        c = (c + 1) % f->cols;
        break;
    default:
        return false;
    }

    f->index = r * f->cols + c;
    return f->index != old;
}

screen_id_t ui_current(void)
{
    return s_sp > 0 ? s_stack[s_sp - 1].id : SCR_HOME;
}

void ui_redraw(void)
{
    s_dirty = true;
}

void ui_toast(const char *msg)
{
    if (!msg) return;
    strncpy(s_toast, msg, sizeof(s_toast) - 1);
    s_toast[sizeof(s_toast) - 1] = '\0';
    s_toast_at = now_ms();
    s_dirty = true;
}

void ui_init(void)
{
    memset(s_stack, 0, sizeof(s_stack));
    s_sp = 0;
    s_stack[0].id = SCR_HOME;
    s_stack[0].arg = NULL;
    s_sp = 1;
    s_ops[SCR_HOME]->enter(NULL);
    s_dirty = true;
    s_last_act = now_ms();
}

void ui_goto(screen_id_t id, void *arg)
{
    if (id >= SCR_COUNT) return;
    /* 已在栈顶就不重复压栈，避免 SET 返回时在同界面空转 */
    if (s_sp > 0 && s_stack[s_sp - 1].id == id) {
        s_ops[id]->enter(arg);
        s_dirty = true;
        return;
    }
    if (s_sp >= SCREEN_STACK_MAX) {
        ESP_LOGW(TAG, "界面栈已满，丢弃最底层");
        memmove(&s_stack[0], &s_stack[1],
                sizeof(stack_entry_t) * (SCREEN_STACK_MAX - 1));
        s_sp = SCREEN_STACK_MAX - 1;
    }
    s_stack[s_sp].id = id;
    s_stack[s_sp].arg = arg;
    s_sp++;
    s_ops[id]->enter(arg);
    s_dirty = true;
    ESP_LOGI(TAG, "进入界面 %d", (int)id);
}

void ui_back(void)
{
    if (s_sp <= 1) {
        s_ops[SCR_HOME]->enter(NULL);
        s_dirty = true;
        return;
    }
    s_sp--;
    screen_id_t id = s_stack[s_sp - 1].id;
    s_ops[id]->enter(s_stack[s_sp - 1].arg);
    s_dirty = true;
}

void ui_tick(void)
{
    int64_t now = now_ms();
    input_poll();

    /* WiFi 状态一变（连上/断开/重连）就重绘，界面上的网络信息才是实时的 */
    wifi_state_t ws = wifi_mgr_state();
    if (ws != s_last_wifi) {
        s_last_wifi = ws;
        s_dirty = true;
    }

    input_event_t ev;
    while (input_next(&ev)) {
        s_last_act = now;

        /* 息屏时任意键只负责唤醒 */
        if (s_asleep) {
            gfx_wake();
            s_asleep = false;
            input_flush();
            s_dirty = true;
            continue;
        }

        /* 全局规则：RST 短按息屏/唤醒，长按 3 秒进二次确认页 */
        if (ev == EV_RST_LONG) {
            /* ⚠️ 已经在确认页时不能再 continue 吞掉事件：
               第二次长按 3 秒必须让 restart_event 收到，它才调 esp_restart()。
               之前这里无条件 ui_goto + continue，导致二次确认永远无效。 */
            if (ui_current() == SCR_RESTART) {
                const screen_ops_t *rs = s_ops[SCR_RESTART];
                if (rs->on_event && rs->on_event(ev)) {
                    s_dirty = true;
                }
                continue;
            }
            ui_goto(SCR_RESTART, NULL);
            continue;
        }
        if (ev == EV_RST) {
            /* 重启确认页里短按 RST 不息屏，避免打断二次确认 */
            if (!s_asleep && ui_current() != SCR_RESTART) {
                gfx_sleep();
                s_asleep = true;
                input_flush();
            }
            continue;
        }
        if (ev == EV_SET_LONG) {
            if (ui_current() != SCR_RESTART) {
                ui_goto(SCR_SETTINGS, NULL);
            }
            continue;
        }
        if (ev == EV_SET) {
            ui_back();
            continue;
        }

        const screen_ops_t *ops = s_ops[ui_current()];
        if (ops->on_event && ops->on_event(ev)) {
            s_dirty = true;
        }
    }

    const screen_ops_t *ops = s_ops[ui_current()];
    if (ops->tick) ops->tick();

    if (s_dirty || s_toast[0]) {
        gfx_fill(g_theme.bg);
        ops->draw();
        if (s_toast[0]) {
            /* 提示条压在底部；用小字并按可用宽度截断，避免长文案显示不全 */
            gfx_rect(8, GFX_H - 38, GFX_W - 8, GFX_H - 10, g_theme.surface_hi);
            char msg[64];
            strncpy(msg, s_toast, sizeof(msg) - 1);
            msg[sizeof(msg) - 1] = '\0';
            int avail = GFX_W - 24;
            while (gfx_text_width_small(msg) > avail && strlen(msg) > 1) {
                size_t n = strlen(msg);
                size_t back = (msg[n - 1] & 0x80) ? 3 : 1;
                msg[n - back] = '\0';
            }
            gfx_text_center_small(GFX_H - 32, msg, g_theme.text, g_theme.surface_hi);
        }
        gfx_flush();
        s_dirty = false;
    }

    if (s_toast[0] && (now - s_toast_at) > TOAST_MS) {
        s_toast[0] = '\0';
        s_dirty = true;
    }

    if (!s_asleep && (now - s_last_act) > IDLE_SLEEP_MS) {
        gfx_sleep();
        s_asleep = true;
        input_flush();
    }
}
