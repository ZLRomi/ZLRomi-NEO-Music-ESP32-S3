#pragma once
#include "types.h"

// 初始化设置模块，从NVS加载
bool settings_init(void);

// 获取设置
const Settings* settings_get(void);
Settings* settings_get_mutable(void);

// 保存到NVS
bool settings_save(void);

// 恢复默认
void settings_reset_defaults(void);

// 单项设置并保存
void settings_set_volume(uint8_t vol);
void settings_set_bt(bool enabled);
void settings_set_wifi(const char* ssid, const char* pass);
void settings_set_quality(AudioQuality q);
