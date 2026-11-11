#pragma once
#include <lvgl.h>
#include "types.h"

// 初始化按键
void input_init();

// 更新按键状态(每loop调用)
void input_update();

// 查询按键
bool input_is_pressed(ButtonId btn);
bool input_was_pressed(ButtonId btn);
bool input_was_long_pressed(ButtonId btn);

// LVGL keypad回调
void input_lvgl_read_cb(lv_indev_t* indev, lv_indev_data_t* data);

// 设置按键回调
typedef void (*input_callback)(ButtonId btn, ButtonEvent evt);
void input_set_callback(input_callback cb);
