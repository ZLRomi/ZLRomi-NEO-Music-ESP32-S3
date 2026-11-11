/**
 * @file settings.cpp
 * @brief ZLRomi NEO Music Player - 设置管理实现
 */

#include "settings.h"
#include <Preferences.h>
#include <string.h>

// NVS命名空间
static const char* NVS_NAMESPACE = "zlr_settings";

// 全局设置实例
static Settings g_settings;
static Preferences g_prefs;
static bool g_initialized = false;

// 默认设置
static Settings make_defaults() {
    Settings s;
    memset(&s, 0, sizeof(s));
    s.volume = 70;
    s.bt_enabled = false;
    s.quality = QUALITY_HIGH;
    s.auto_play = true;
    s.screen_brightness = 200;
    return s;
}
static const Settings DEFAULT_SETTINGS = make_defaults();

bool settings_init(void) {
    if (g_initialized) return true;
    
    // 打开NVS命名空间
    if (!g_prefs.begin(NVS_NAMESPACE, false)) {
        Serial.println("[Settings] NVS open failed, using defaults");
        memcpy(&g_settings, &DEFAULT_SETTINGS, sizeof(Settings));
        g_initialized = true;
        return false;
    }
    
    // 检查是否有已保存的设置(通过版本号标记)
    uint8_t version = g_prefs.getUChar("ver", 0);
    if (version == 1) {
        // 读取已保存的设置
        g_settings.volume = g_prefs.getUChar("vol", DEFAULT_SETTINGS.volume);
        g_settings.bt_enabled = g_prefs.getBool("bt_en", DEFAULT_SETTINGS.bt_enabled);
        g_prefs.getString("wifi_ssid", g_settings.wifi_ssid, sizeof(g_settings.wifi_ssid));
        g_prefs.getString("wifi_pass", g_settings.wifi_pass, sizeof(g_settings.wifi_pass));
        g_settings.quality = (AudioQuality)g_prefs.getUChar("quality", DEFAULT_SETTINGS.quality);
        g_settings.auto_play = g_prefs.getBool("auto_play", DEFAULT_SETTINGS.auto_play);
        g_settings.screen_brightness = g_prefs.getUChar("bright", DEFAULT_SETTINGS.screen_brightness);
        Serial.println("[Settings] Loaded from NVS");
    } else {
        // 首次运行，使用默认值并保存
        memcpy(&g_settings, &DEFAULT_SETTINGS, sizeof(Settings));
        settings_save();
        Serial.println("[Settings] Using defaults");
    }
    
    g_initialized = true;
    return true;
}

const Settings* settings_get(void) {
    return &g_settings;
}

Settings* settings_get_mutable(void) {
    return &g_settings;
}

bool settings_save(void) {
    if (!g_initialized) return false;
    
    g_prefs.putUChar("ver", 1);
    g_prefs.putUChar("vol", g_settings.volume);
    g_prefs.putBool("bt_en", g_settings.bt_enabled);
    g_prefs.putString("wifi_ssid", g_settings.wifi_ssid);
    g_prefs.putString("wifi_pass", g_settings.wifi_pass);
    g_prefs.putUChar("quality", (uint8_t)g_settings.quality);
    g_prefs.putBool("auto_play", g_settings.auto_play);
    g_prefs.putUChar("bright", g_settings.screen_brightness);
    
    Serial.printf("[Settings] Saved: vol=%d, bt=%d, quality=%d\n",
                  g_settings.volume, g_settings.bt_enabled, g_settings.quality);
    return true;
}

void settings_reset_defaults(void) {
    memcpy(&g_settings, &DEFAULT_SETTINGS, sizeof(Settings));
    settings_save();
    Serial.println("[Settings] Reset to defaults");
}

void settings_set_volume(uint8_t vol) {
    if (vol > 100) vol = 100;
    g_settings.volume = vol;
    settings_save();
}

void settings_set_bt(bool enabled) {
    g_settings.bt_enabled = enabled;
    settings_save();
}

void settings_set_wifi(const char* ssid, const char* pass) {
    if (ssid) {
        strncpy(g_settings.wifi_ssid, ssid, sizeof(g_settings.wifi_ssid) - 1);
        g_settings.wifi_ssid[sizeof(g_settings.wifi_ssid) - 1] = '\0';
    }
    if (pass) {
        strncpy(g_settings.wifi_pass, pass, sizeof(g_settings.wifi_pass) - 1);
        g_settings.wifi_pass[sizeof(g_settings.wifi_pass) - 1] = '\0';
    }
    settings_save();
}

void settings_set_quality(AudioQuality q) {
    g_settings.quality = q;
    settings_save();
}
