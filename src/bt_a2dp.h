/**
 * @file bt_a2dp.h
 * @brief Bluetooth A2DP Source for NEO Music Player
 * Uses ESP-IDF native BT API (not ESP32-A2DP library)
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Initialize Bluetooth A2DP Source
// Device will be discoverable as "NEO-Music"
void bt_a2dp_init(void);

// Check if connected to a sink (speaker)
bool bt_a2dp_is_connected(void);

// Write PCM audio data to A2DP stream
// data: interleaved 16-bit stereo PCM samples
// len: number of bytes
void bt_a2dp_write(const uint8_t* data, size_t len);

// Set volume (0-127)
void bt_a2dp_set_volume(int vol);

// Stop and cleanup
void bt_a2dp_stop(void);
