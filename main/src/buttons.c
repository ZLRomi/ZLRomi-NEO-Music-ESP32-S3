/*
 * buttons —— HW-678 ESP32-S3 7键导航实现
 */
#include "buttons.h"
#include <stdbool.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_timer.h"

/* HW-678 按键引脚 */
#define PIN_UP     17
#define PIN_DOWN   16
#define PIN_LEFT   15
#define PIN_RIGHT   4
#define PIN_OK      5
#define PIN_SET     6
#define PIN_RST     7

#define DEBOUNCE_MS    20
#define LONG_PRESS_MS  800

typedef struct {
    int      gpio;
    btn_id_t id;
    bool     raw;
    bool     stable;
    bool     long_fired;
    int64_t  changed_at;
    int64_t  press_start;
} btn_state_t;

static btn_state_t s_btns[] = {
    { PIN_UP,    BTN_UP,    false, false, false, 0, 0 },
    { PIN_DOWN,  BTN_DOWN,  false, false, false, 0, 0 },
    { PIN_LEFT,  BTN_LEFT,  false, false, false, 0, 0 },
    { PIN_RIGHT, BTN_RIGHT, false, false, false, 0, 0 },
    { PIN_OK,    BTN_OK,    false, false, false, 0, 0 },
    { PIN_SET,   BTN_SET,   false, false, false, 0, 0 },
    { PIN_RST,   BTN_RST,   false, false, false, 0, 0 },
};

#define BTN_COUNT  (sizeof(s_btns) / sizeof(s_btns[0]))

void buttons_init(void)
{
    uint64_t mask = 0;
    for (size_t i = 0; i < BTN_COUNT; i++) {
        mask |= (1ULL << s_btns[i].gpio);
    }
    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

static void buttons_update(void)
{
    int64_t now = esp_timer_get_time() / 1000;
    for (size_t i = 0; i < BTN_COUNT; i++) {
        bool raw = (gpio_get_level(s_btns[i].gpio) == 0);
        if (raw != s_btns[i].raw) {
            s_btns[i].raw = raw;
            s_btns[i].changed_at = now;
        }
        if (raw != s_btns[i].stable && (now - s_btns[i].changed_at) >= DEBOUNCE_MS) {
            s_btns[i].stable = raw;
            if (raw) {
                s_btns[i].press_start = now;
                s_btns[i].long_fired = false;
            }
        }
    }
}

btn_id_t buttons_read(void)
{
    buttons_update();
    for (size_t i = 0; i < BTN_COUNT; i++) {
        if (s_btns[i].stable) return s_btns[i].id;
    }
    return BTN_NONE;
}

btn_id_t buttons_pressed(void)
{
    static btn_id_t prev = BTN_NONE;
    buttons_update();
    btn_id_t cur = BTN_NONE;
    for (size_t i = 0; i < BTN_COUNT; i++) {
        if (s_btns[i].stable) { cur = s_btns[i].id; break; }
    }
    btn_id_t ev = (cur != BTN_NONE && cur != prev) ? cur : BTN_NONE;
    prev = cur;
    return ev;
}

btn_id_t buttons_long_press(void)
{
    int64_t now = esp_timer_get_time() / 1000;
    buttons_update();
    for (size_t i = 0; i < BTN_COUNT; i++) {
        if (s_btns[i].stable && !s_btns[i].long_fired) {
            if ((now - s_btns[i].press_start) >= LONG_PRESS_MS) {
                s_btns[i].long_fired = true;
                return s_btns[i].id;
            }
        }
    }
    return BTN_NONE;
}

const char *buttons_name(btn_id_t id)
{
    switch (id) {
    case BTN_UP:    return "UP";
    case BTN_DOWN:  return "DOWN";
    case BTN_LEFT:  return "LEFT";
    case BTN_RIGHT: return "RIGHT";
    case BTN_OK:    return "OK";
    case BTN_SET:   return "SET";
    case BTN_RST:   return "RST";
    default:        return "-";
    }
}
