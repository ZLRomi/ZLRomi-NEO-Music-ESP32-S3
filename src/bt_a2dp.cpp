/**
 * @file bt_a2dp.cpp
 * @brief Bluetooth A2DP Source implementation using ESP-IDF native API
 * 
 * Note: This requires CONFIG_BT_ENABLED and CONFIG_BT_A2DP_ENABLE
 * in sdkconfig. For Arduino framework, these may need to be set via
 * build_flags or a custom sdkconfig.defaults file.
 */
#include "bt_a2dp.h"
#include <Arduino.h>

// ESP-IDF BT headers (available in Arduino-ESP32 core)
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

static const char* TAG = "BT_A2DP";
static bool s_connected = false;
static bool s_initialized = false;
static RingbufHandle_t s_ringbuf = NULL;
static uint8_t s_volume = 80;

// ---- A2DP callbacks ----

static void a2dp_source_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                s_connected = true;
                Serial.println("[BT] A2DP connected");
            } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                s_connected = false;
                Serial.println("[BT] A2DP disconnected");
            }
            break;
        case ESP_A2D_AUDIO_STATE_EVT:
            Serial.printf("[BT] Audio state: %d\n", param->audio_stat.state);
            break;
        default:
            break;
    }
}

static int32_t a2dp_data_cb(uint8_t *data, int32_t len) {
    if (!s_connected || !s_ringbuf) return 0;
    
    size_t items_size = 0;
    uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(s_ringbuf, &items_size, 0, len);
    if (item) {
        memcpy(data, item, items_size);
        vRingbufferReturnItem(s_ringbuf, item);
        return items_size;
    }
    return 0;
}

// ---- Public API ----

void bt_a2dp_init(void) {
    if (s_initialized) return;
    
    Serial.println("[BT] Initializing A2DP Source...");
    
    // Create ring buffer for audio data (16KB)
    s_ringbuf = xRingbufferCreate(16384, RINGBUF_TYPE_BYTEBUF);
    if (!s_ringbuf) {
        Serial.println("[BT] Failed to create ring buffer");
        return;
    }
    
    // Release classic BT memory
    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        Serial.printf("[BT] BLE mem release failed: %d (may be OK)\n", ret);
    }
    
    // Initialize BT controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        Serial.printf("[BT] Controller init failed: %d\n", ret);
        return;
    }
    
    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        Serial.printf("[BT] Controller enable failed: %d\n", ret);
        return;
    }
    
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        Serial.printf("[BT] Bluedroid init failed: %d\n", ret);
        return;
    }
    
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        Serial.printf("[BT] Bluedroid enable failed: %d\n", ret);
        return;
    }
    
    // Set device name
    esp_bt_dev_set_device_name("NEO-Music");
    
    // Register A2DP source callback
    esp_a2d_register_callback(a2dp_source_cb);
    esp_a2d_source_register_data_callback(a2dp_data_cb);
    
    // Initialize A2DP source
    ret = esp_a2d_source_init();
    if (ret != ESP_OK) {
        Serial.printf("[BT] A2DP source init failed: %d\n", ret);
        return;
    }
    
    // Set discoverable and connectable
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    
    s_initialized = true;
    Serial.println("[BT] A2DP Source ready, discoverable as 'NEO-Music'");
}

bool bt_a2dp_is_connected(void) {
    return s_connected;
}

void bt_a2dp_write(const uint8_t* data, size_t len) {
    if (!s_connected || !s_ringbuf || !data || len == 0) return;
    
    // Try to send data to ring buffer (non-blocking)
    BaseType_t result = xRingbufferSend(s_ringbuf, data, len, 0);
    if (result != pdTRUE) {
        // Buffer full, discard oldest data
        size_t discard_size = 0;
        uint8_t *discard = (uint8_t *)xRingbufferReceiveUpTo(s_ringbuf, &discard_size, 0, len);
        if (discard) {
            vRingbufferReturnItem(s_ringbuf, discard);
        }
        xRingbufferSend(s_ringbuf, data, len, 0);
    }
}

void bt_a2dp_set_volume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    s_volume = vol;
    // AVRCP volume control would go here
    // esp_avrc_ct_send_set_absolute_volume_cmd() requires AVRC connection
}

void bt_a2dp_stop(void) {
    if (!s_initialized) return;
    esp_a2d_source_deinit();
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    if (s_ringbuf) {
        vRingbufferDelete(s_ringbuf);
        s_ringbuf = NULL;
    }
    s_initialized = false;
    s_connected = false;
    Serial.println("[BT] A2DP stopped");
}
