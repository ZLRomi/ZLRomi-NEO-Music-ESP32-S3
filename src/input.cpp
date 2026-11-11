#include "input.h"
#include <Arduino.h>

// ---------- pin mapping ----------
static const uint8_t PIN_MAP[BTN_COUNT] = {
    17, // BTN_UP
    16, // BTN_DOWN
    15, // BTN_LEFT
     4, // BTN_RIGHT
     5, // BTN_OK
     6, // BTN_SET
     7  // BTN_RST
};

// ---------- timing constants ----------
static const uint32_t DEBOUNCE_MS   = 30;
static const uint32_t LONG_PRESS_MS = 800;

// ---------- per-button state ----------
struct BtnState {
    bool     raw;           // current physical reading (true=pressed/LOW)
    bool     stable;        // debounced state
    bool     prev_stable;   // previous debounced state
    bool     press_fired;   // single-press already consumed
    bool     long_fired;    // long-press already fired this hold
    uint32_t change_time;   // last raw-change timestamp
    uint32_t press_time;    // timestamp when stable press began
};

static BtnState _btns[BTN_COUNT];
static input_callback _cb = nullptr;

// LVGL integration state
static lv_key_t _lvgl_last_key = LV_KEY_ENTER;
static bool     _lvgl_pressed  = false;

// ---------- internal helpers ----------

static void btn_init_pins() {
    for (int i = 0; i < BTN_COUNT; i++) {
        pinMode(PIN_MAP[i], INPUT_PULLUP);
    }
}

static void btn_update_one(int idx) {
    BtnState& b = _btns[idx];
    bool reading = (digitalRead(PIN_MAP[idx]) == LOW); // pressed = LOW

    // debounce: only accept change if stable for DEBOUNCE_MS
    if (reading != b.raw) {
        b.raw = reading;
        b.change_time = millis();
        return; // wait for debounce window
    }
    if ((millis() - b.change_time) < DEBOUNCE_MS) return;

    b.prev_stable = b.stable;
    b.stable = reading;

    // rising edge → just pressed
    if (b.stable && !b.prev_stable) {
        b.press_time = millis();
        b.press_fired = false;
        b.long_fired = false;
        if (_cb) _cb((ButtonId)idx, EVT_PRESS);
    }

    // held and not yet long-fired
    if (b.stable && !b.long_fired) {
        if ((millis() - b.press_time) >= LONG_PRESS_MS) {
            b.long_fired = true;
            if (_cb) _cb((ButtonId)idx, EVT_LONG_PRESS);
        }
    }

    // falling edge → released
    if (!b.stable && b.prev_stable) {
        if (_cb) _cb((ButtonId)idx, EVT_RELEASE);
    }
}

// Map ButtonId → LVGL key
static lv_key_t btn_to_lvgl_key(ButtonId btn) {
    switch (btn) {
        case BTN_UP:    return LV_KEY_UP;
        case BTN_DOWN:  return LV_KEY_DOWN;
        case BTN_LEFT:  return LV_KEY_LEFT;
        case BTN_RIGHT: return LV_KEY_RIGHT;
        case BTN_OK:    return LV_KEY_ENTER;
        case BTN_SET:   return LV_KEY_ESC;
        case BTN_RST:   return LV_KEY_ESC;
        default:        return LV_KEY_ENTER;
    }
}

// Find the currently active button for LVGL (priority: first pressed)
static ButtonId find_active_btn() {
    for (int i = 0; i < BTN_COUNT; i++) {
        if (_btns[i].stable) return (ButtonId)i;
    }
    return BTN_COUNT; // none
}

// ---------- public API ----------

void input_init() {
    memset(_btns, 0, sizeof(_btns));
    btn_init_pins();
    // seed raw readings so first debounce window is valid
    uint32_t now = millis();
    for (int i = 0; i < BTN_COUNT; i++) {
        _btns[i].raw = (digitalRead(PIN_MAP[i]) == LOW);
        _btns[i].change_time = now;
    }
}

void input_update() {
    for (int i = 0; i < BTN_COUNT; i++) {
        btn_update_one(i);
    }
}

bool input_is_pressed(ButtonId btn) {
    if (btn < 0 || btn >= BTN_COUNT) return false;
    return _btns[btn].stable;
}

bool input_was_pressed(ButtonId btn) {
    if (btn < 0 || btn >= BTN_COUNT) return false;
    BtnState& b = _btns[btn];
    // fire once on the stable-press edge, then consume
    if (b.stable && !b.press_fired) {
        b.press_fired = true;
        return true;
    }
    return false;
}

bool input_was_long_pressed(ButtonId btn) {
    if (btn < 0 || btn >= BTN_COUNT) return false;
    // long_fired is set exactly once per hold in btn_update_one
    // We expose it as a one-shot by checking and clearing via a flag.
    // Since btn_update_one already fires the callback, here we just report state.
    // For polling-style usage, return true while long_fired and still held.
    return _btns[btn].long_fired && _btns[btn].stable;
}

void input_set_callback(input_callback cb) {
    _cb = cb;
}

void input_lvgl_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;

    ButtonId active = find_active_btn();
    if (active < BTN_COUNT) {
        _lvgl_last_key = btn_to_lvgl_key(active);
        _lvgl_pressed = true;
    } else {
        _lvgl_pressed = false;
    }

    data->key   = _lvgl_last_key;
    data->state = _lvgl_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
