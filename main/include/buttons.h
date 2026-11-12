/*
 * buttons —— HW-678 ESP32-S3 7键导航
 * 
 * UP=GPIO17, DOWN=GPIO16, LEFT=GPIO15, RIGHT=GPIO4
 * OK=GPIO5, SET=GPIO6, RST=GPIO7
 * 
 * 内部上拉，按下导通到 GND（低电平有效）
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTN_NONE = 0,
    BTN_UP,      /* GPIO17 */
    BTN_DOWN,    /* GPIO16 */
    BTN_LEFT,    /* GPIO15 */
    BTN_RIGHT,   /* GPIO4  */
    BTN_OK,      /* GPIO5  */
    BTN_SET,     /* GPIO6  */
    BTN_RST,     /* GPIO7  */
} btn_id_t;

/** @brief 初始化 7 个按键引脚（内部上拉） */
void buttons_init(void);

/** @brief 读当前按下的键（带 20ms 消抖） */
btn_id_t buttons_read(void);

/** @brief 取一次「刚按下」的边沿事件 */
btn_id_t buttons_pressed(void);

/** @brief 长按检测 (>800ms) */
btn_id_t buttons_long_press(void);

/** @brief 按键名，调试打印用 */
const char *buttons_name(btn_id_t id);

#ifdef __cplusplus
}
#endif
