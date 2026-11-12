#include "input.h"

#include <string.h>

#include "board.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define DEBOUNCE_MS   20
#define LONG_MS       600
#define REPEAT_MS     120
#define RST_LONG_MS   3000
#define QUEUE_SIZE    24

typedef enum {
    K_UP = 0, K_DOWN, K_LEFT, K_RIGHT, K_OK, K_SET, K_RST, K_COUNT
} key_id_t;

typedef struct {
    int          gpio;
    input_event_t click;
    input_event_t hold;
    bool         raw;
    bool         stable;
    bool         down;
    bool         long_sent;
    bool         muted;
    int64_t      changed_at;
    int64_t      down_ms;
    int64_t      next_repeat;
} key_state_t;

static key_state_t s_keys[K_COUNT] = {
    [K_UP]    = { KEY_PIN_UP,    EV_UP,    EV_UP_REPEAT,    false, false, false, false, false, 0, 0, 0 },
    [K_DOWN]  = { KEY_PIN_DOWN,  EV_DOWN,  EV_DOWN_REPEAT,  false, false, false, false, false, 0, 0, 0 },
    [K_LEFT]  = { KEY_PIN_LEFT,  EV_LEFT,  EV_LEFT_REPEAT,  false, false, false, false, false, 0, 0, 0 },
    [K_RIGHT] = { KEY_PIN_RIGHT, EV_RIGHT, EV_RIGHT_REPEAT, false, false, false, false, false, 0, 0, 0 },
    [K_OK]    = { KEY_PIN_OK,    EV_OK,    EV_OK_LONG,      false, false, false, false, false, 0, 0, 0 },
    [K_SET]   = { KEY_PIN_SET,   EV_SET,   EV_SET_LONG,     false, false, false, false, false, 0, 0, 0 },
    [K_RST]   = { KEY_PIN_RST,   EV_RST,   EV_RST_LONG,     false, false, false, false, false, 0, 0, 0 },
};

static input_event_t s_q[QUEUE_SIZE];
static int s_head, s_tail;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void push(input_event_t ev)
{
    if (ev == EV_NONE) {
        return;
    }
    int next = (s_head + 1) % QUEUE_SIZE;
    if (next == s_tail) {
        return;
    }
    s_q[s_head] = ev;
    s_head = next;
}

static bool repeatable(key_id_t id)
{
    return id == K_UP || id == K_DOWN || id == K_LEFT || id == K_RIGHT;
}

void input_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < K_COUNT; i++) {
        mask |= (1ULL << s_keys[i].gpio);
    }
    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    memset(s_q, 0, sizeof(s_q));
    s_head = s_tail = 0;
}

void input_poll(void)
{
    int64_t now = now_ms();

    for (int i = 0; i < K_COUNT; i++) {
        key_state_t *k = &s_keys[i];
        bool level = (gpio_get_level(k->gpio) == 0);   /* 按下为低 */

        /* 20ms 消抖：电平稳定足够久才更新 */
        if (level != k->raw) {
            k->raw = level;
            k->changed_at = now;
        }
        if (k->raw != k->stable && (now - k->changed_at) >= DEBOUNCE_MS) {
            k->stable = k->raw;
        }

        bool pressed = k->stable;
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
                push(k->click);
            }
            k->long_sent   = false;
            k->next_repeat = 0;
        }

        if (!k->down || k->muted) {
            continue;
        }

        int64_t threshold = (i == K_RST) ? RST_LONG_MS : LONG_MS;
        if (!k->long_sent && (now - k->down_ms) >= threshold) {
            k->long_sent   = true;
            k->next_repeat = now + REPEAT_MS;
            push(k->hold);
        } else if (k->long_sent && repeatable((key_id_t)i) && now >= k->next_repeat) {
            k->next_repeat = now + REPEAT_MS;
            push(k->hold);
        }
    }
}

bool input_next(input_event_t *ev)
{
    if (s_tail == s_head) {
        return false;
    }
    *ev = s_q[s_tail];
    s_tail = (s_tail + 1) % QUEUE_SIZE;
    return true;
}

void input_flush(void)
{
    s_tail = s_head;
    for (int i = 0; i < K_COUNT; i++) {
        /* 息屏唤醒时正按着的键：松手不补发单击，按住也不连发 */
        s_keys[i].muted       = s_keys[i].down;
        s_keys[i].long_sent   = false;
        s_keys[i].next_repeat = 0;
    }
}
