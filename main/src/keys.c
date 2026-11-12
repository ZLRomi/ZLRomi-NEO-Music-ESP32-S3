/*
 * keys —— HW-678 7 键事件状态机
 *
 * 事件语义（与 keys.h 的 key_event_t 一一对应）：
 *   UP / DOWN / LEFT / RIGHT
 *       短按           -> KEY_xxx
 *       长按 600ms 起  -> 每 120ms 连发 KEY_xxx_REPEAT（列表滚动、切歌、调音量）
 *   OK / SET
 *       短按           -> KEY_OK / KEY_SET
 *       长按 600ms     -> KEY_OK_LONG / KEY_SET_LONG（只发一次）
 *   RST
 *       长按 800ms     -> KEY_RST_LONG（交给 UI 做重启），短按不产生事件
 *
 * 已经发过长按/连发的键，松手时不再补发短按，避免「切完模式又暂停一次」。
 * keys_flush() 会静音当前正按着的键，直到松手为止（息屏唤醒用）。
 */

#include "keys.h"

#include <string.h>

#include "buttons.h"
#include "esp_timer.h"

#define LONG_MS      600
#define REPEAT_MS    120
#define RST_LONG_MS  800
#define QSIZE        16

typedef struct {
    btn_id_t id;
    bool     down;             /* 当前是否按着 */
    bool     long_sent;        /* 本次按下已产生过长按/连发事件 */
    bool     muted;            /* flush 后静音，松手前不产生任何事件 */
    int64_t  down_ms;
    int64_t  next_repeat;
} key_t;

static key_t s_keys[] = {
    { BTN_UP,    false, false, false, 0, 0 },
    { BTN_DOWN,  false, false, false, 0, 0 },
    { BTN_LEFT,  false, false, false, 0, 0 },
    { BTN_RIGHT, false, false, false, 0, 0 },
    { BTN_OK,    false, false, false, 0, 0 },
    { BTN_SET,   false, false, false, 0, 0 },
    { BTN_RST,   false, false, false, 0, 0 },
};
#define KEY_COUNT  (sizeof(s_keys) / sizeof(s_keys[0]))

static key_event_t s_q[QSIZE];
static int         s_head, s_tail;

static void push(key_event_t ev)
{
    int next = (s_head + 1) % QSIZE;
    if (next == s_tail) {
        return;                 /* 满了就丢，按键事件不排长队 */
    }
    s_q[s_head] = ev;
    s_head      = next;
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* 只有四个方向键支持长按连发 */
static bool repeatable(btn_id_t id)
{
    switch (id) {
    case BTN_UP:
    case BTN_DOWN:
    case BTN_LEFT:
    case BTN_RIGHT:
        return true;
    case BTN_OK:
    case BTN_SET:
    case BTN_RST:
    case BTN_NONE:
    default:
        return false;
    }
}

static key_event_t click_of(btn_id_t id)
{
    switch (id) {
    case BTN_UP:    return KEY_UP;
    case BTN_DOWN:  return KEY_DOWN;
    case BTN_LEFT:  return KEY_LEFT;
    case BTN_RIGHT: return KEY_RIGHT;
    case BTN_OK:    return KEY_OK;
    case BTN_SET:   return KEY_SET;
    case BTN_RST:   return KEY_NONE;   /* RST 短按无动作 */
    case BTN_NONE:  return KEY_NONE;
    default:        return KEY_NONE;
    }
}

/* 长按首事件：方向键是连发的第一下，OK/SET/RST 是一次性长按事件 */
static key_event_t hold_of(btn_id_t id)
{
    switch (id) {
    case BTN_UP:    return KEY_UP_REPEAT;
    case BTN_DOWN:  return KEY_DOWN_REPEAT;
    case BTN_LEFT:  return KEY_LEFT_REPEAT;
    case BTN_RIGHT: return KEY_RIGHT_REPEAT;
    case BTN_OK:    return KEY_OK_LONG;
    case BTN_SET:   return KEY_SET_LONG;
    case BTN_RST:   return KEY_RST_LONG;
    case BTN_NONE:  return KEY_NONE;
    default:        return KEY_NONE;
    }
}

void keys_init(void)
{
    buttons_init();
    memset(s_keys, 0, sizeof(s_keys));
    /* memset 后重新写上按键 id，其余状态保持全 0 */
    s_keys[0].id = BTN_UP;
    s_keys[1].id = BTN_DOWN;
    s_keys[2].id = BTN_LEFT;
    s_keys[3].id = BTN_RIGHT;
    s_keys[4].id = BTN_OK;
    s_keys[5].id = BTN_SET;
    s_keys[6].id = BTN_RST;
    s_head = s_tail = 0;
}

void keys_poll(void)
{
    int64_t  now = now_ms();
    btn_id_t cur = buttons_read();

    for (size_t i = 0; i < KEY_COUNT; i++) {
        key_t *k       = &s_keys[i];
        bool   pressed = (cur == k->id);

        if (pressed && !k->down) {
            k->down        = true;
            k->down_ms     = now;
            k->long_sent   = false;
            k->next_repeat = 0;
        } else if (!pressed && k->down) {
            k->down = false;
            bool was_muted = k->muted;
            k->muted = false;
            if (!was_muted && !k->long_sent) {
                key_event_t ev = click_of(k->id);
                if (ev != KEY_NONE) {
                    push(ev);
                }
            }
            k->long_sent   = false;
            k->next_repeat = 0;
        }

        if (!k->down || k->muted) {
            continue;
        }

        int64_t threshold = (k->id == BTN_RST) ? RST_LONG_MS : LONG_MS;
        if (!k->long_sent && (now - k->down_ms) >= threshold) {
            k->long_sent   = true;
            k->next_repeat = now + REPEAT_MS;
            key_event_t ev = hold_of(k->id);
            if (ev != KEY_NONE) {
                push(ev);
            }
        } else if (k->long_sent && repeatable(k->id) && now >= k->next_repeat) {
            k->next_repeat = now + REPEAT_MS;
            key_event_t ev = hold_of(k->id);
            if (ev != KEY_NONE) {
                push(ev);
            }
        }
    }
}

bool keys_next(key_event_t *ev)
{
    if (s_tail == s_head) {
        return false;
    }
    *ev    = s_q[s_tail];
    s_tail = (s_tail + 1) % QSIZE;
    return true;
}

void keys_flush(void)
{
    s_tail = s_head;
    for (size_t i = 0; i < KEY_COUNT; i++) {
        key_t *k = &s_keys[i];
        /* 息屏唤醒时正按着的那个键，松手不能补发单击，按住也不能连发。
           否则唤醒动作会顺带切歌或一直调音量。 */
        k->muted          = k->down;
        k->long_sent      = false;
        k->next_repeat    = 0;
    }
}
