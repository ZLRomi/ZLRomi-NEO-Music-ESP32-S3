/*
 * keys —— HW-678 7键事件状态机
 *
 * UP/DOWN/LEFT/RIGHT: 导航 + 长按连发
 * OK: 确认/播放暂停
 * SET: 返回/菜单
 * RST: 长按重启
 */
#pragma once

#include <stdbool.h>
#include "buttons.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KEY_NONE = 0,
    KEY_UP, KEY_UP_REPEAT,
    KEY_DOWN, KEY_DOWN_REPEAT,
    KEY_LEFT, KEY_LEFT_REPEAT,
    KEY_RIGHT, KEY_RIGHT_REPEAT,
    KEY_OK, KEY_OK_LONG,
    KEY_SET, KEY_SET_LONG,
    KEY_RST_LONG,
} key_event_t;

void keys_init(void);
void keys_poll(void);
bool keys_next(key_event_t *ev);
void keys_flush(void);

#ifdef __cplusplus
}
#endif
