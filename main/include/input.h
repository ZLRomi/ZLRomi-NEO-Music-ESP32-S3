/*
 * input.h —— 7 键输入（无触摸屏）
 *
 * 电视遥控风格：UP/DOWN/LEFT/RIGHT 移动焦点，OK 确认，SET 返回/设置。
 * 方向键长按连发；OK/SET 长按出独立事件。
 * RST：短按息屏/唤醒；长按 3 秒弹出电源页（重启 / 关机）。
 */
#pragma once

#include <stdbool.h>

typedef enum {
    EV_NONE = 0,
    EV_UP, EV_UP_REPEAT,
    EV_DOWN, EV_DOWN_REPEAT,
    EV_LEFT, EV_LEFT_REPEAT,
    EV_RIGHT, EV_RIGHT_REPEAT,
    EV_OK, EV_OK_LONG,
    EV_SET, EV_SET_LONG,
    EV_RST, EV_RST_LONG,
} input_event_t;

void input_init(void);
void input_poll(void);
bool input_next(input_event_t *ev);
void input_flush(void);
