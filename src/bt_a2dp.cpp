/**
 * @file bt_a2dp.cpp
 * @brief Bluetooth A2DP Source using ESP32-A2DP library v1.8.9
 */
#include "bt_a2dp.h"
#include <Arduino.h>
#include "BluetoothA2DPSource.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

static BluetoothA2DPSource s_a2dp_source;
static bool s_connected = false;
static RingbufHandle_t s_ringbuf = NULL;
static int32_t s_volume = 80;

// A2DP data callback - provides PCM data to BT stack
static int32_t a2dp_data_callback(uint8_t *data, int32_t len) {
    if (!s_connected || !s_ringbuf) return 0;
    
    size_t items_size = 0;
    uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(s_ringbuf, &items_size, 0, len);
    if (item) {
        memcpy(data, item, items_size);
        vRingbufferReturnItem(s_ringbuf, item);
        return items_size;
    }
    // Return silence if no data
    memset(data, 0, len);
    return len;
}

// Connection state callback
static void a2dp_state_callback(bool connected) {
    s_connected = connected;
    Serial.printf("[BT] A2DP %s\n", connected ? "connected" : "disconnected");
}

void bt_a2dp_init(void) {
    Serial.println("[BT] Initializing A2DP Source...");
    
    // Create ring buffer for audio data (32KB)
    s_ringbuf = xRingbufferCreate(32768, RINGBUF_TYPE_BYTEBUF);
    if (!s_ringbuf) {
        Serial.println("[BT] Failed to create ring buffer");
        return;
    }
    
    // Configure and start A2DP source
    s_a2dp_source.set_on_connection_state_changed(a2dp_state_callback);
    s_a2dp_source.set_volume(s_volume);
    s_a2dp_source.start("NEO-Music", a2dp_data_callback);
    
    Serial.println("[BT] A2DP Source started, discoverable as 'NEO-Music'");
}

bool bt_a2dp_is_connected(void) {
    return s_connected;
}

void bt_a2dp_write(const uint8_t* data, size_t len) {
    if (!s_connected || !s_ringbuf || !data || len == 0) return;
    
    BaseType_t result = xRingbufferSend(s_ringbuf, data, len, pdMS_TO_TICKS(10));
    if (result != pdTRUE) {
        // Buffer full, discard oldest
        size_t discard_size = 0;
        uint8_t *discard = (uint8_t *)xRingbufferReceiveUpTo(s_ringbuf, &discard_size, 0, len);
        if (discard) vRingbufferReturnItem(s_ringbuf, discard);
        xRingbufferSend(s_ringbuf, data, len, 0);
    }
}

void bt_a2dp_set_volume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    s_volume = vol;
    s_a2dp_source.set_volume(vol);
}

void bt_a2dp_stop(void) {
    s_a2dp_source.disconnect();
    if (s_ringbuf) {
        vRingbufferDelete(s_ringbuf);
        s_ringbuf = NULL;
    }
    s_connected = false;
    Serial.println("[BT] A2DP stopped");
}
