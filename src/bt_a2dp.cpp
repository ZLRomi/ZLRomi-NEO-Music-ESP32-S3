/**
 * @file bt_a2dp.cpp
 * @brief Bluetooth A2DP Source - STUB implementation
 * 
 * Full BT A2DP requires ESP-IDF framework or custom sdkconfig.
 * This stub allows compilation with Arduino framework.
 * TODO: Switch to ESP-IDF framework for full BT support.
 */
#include "bt_a2dp.h"
#include <Arduino.h>

static bool s_connected = false;

void bt_a2dp_init(void) {
    Serial.println("[BT] A2DP stub - not available in Arduino framework");
    Serial.println("[BT] To enable: switch to ESP-IDF framework or use ESP32-A2DP library");
}

bool bt_a2dp_is_connected(void) {
    return s_connected;
}

void bt_a2dp_write(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
}

void bt_a2dp_set_volume(int vol) {
    (void)vol;
}

void bt_a2dp_stop(void) {
}
