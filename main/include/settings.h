/*
 * settings.h —— 持久化设置（NVS）
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t volume;        /* 0~100 */
    uint8_t brightness;    /* 0~100 */
    uint8_t rotation;      /* 0~3 */
    uint8_t quality;       /* 0 标准 / 1 较高 / 2 无损 */
    uint8_t play_mode;     /* 0 顺序 / 1 单曲循环 / 2 随机 */
    uint8_t boot_anim;     /* 开机动画开关 */
    char    wifi_ssid[33];
    char    wifi_pass[65];
} app_settings_t;

extern app_settings_t g_cfg;

/** 需在 nvs_flash_init() 之后调用 */
void settings_init(void);
void settings_save(void);
