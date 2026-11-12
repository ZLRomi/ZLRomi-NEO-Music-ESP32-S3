/**
 * @file bt_a2dp.cpp
 * @brief Bluetooth A2DP - NOT SUPPORTED on ESP32-S3
 * 
 * ESP32-S3 only has BLE (no Classic Bluetooth BR/EDR).
 * A2DP requires Classic Bluetooth, which is not available on S3.
 * Use I2S PCM5102A for wired audio output instead.
 */
#include "bt_a2dp.h"
#include <Arduino.h>

void bt_a2dp_init(void) {
    Serial.println("[BT] A2DP not supported on ESP32-S3 (no Classic BT)");
    Serial.println("[BT] Use I2S PCM5102A for audio output");
}

bool bt_a2dp_is_connected(void) { return false; }
void bt_a2dp_write(const uint8_t* data, size_t len) { (void)data; (void)len; }
void bt_a2dp_set_volume(int vol) { (void)vol; }
void bt_a2dp_stop(void) {}
