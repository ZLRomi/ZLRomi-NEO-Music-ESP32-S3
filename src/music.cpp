/**
 * @file music.cpp
 * @brief Music player - ESP32-audioI2S + Netease Cloud API
 * PCM5102A I2S: BCK=9, LRCK=11, DIN=10
 */
#include "music.h"
#include <Audio.h>
#include "ui.h"
#include "config.h"

// I2S pins for HW-678 PCM5102A
#define I2S_BCLK   9
#define I2S_LRC    11
#define I2S_DOUT   10

static Audio audio;

void music_init()
{
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(Music.volume_value);
    Serial.println("[Music] Audio engine initialized");
}

void music_read()
{
    // No SD card scanning needed - we use network streaming
    Serial.println("[Music] Ready for network streaming");
}

void music_update()
{
    audio.loop();
    audio.setVolume(Music.volume_value);

    if (Music.play_flag) {
        // Start playback
        if (Music.start_flag) {
            // TODO: connect to stream URL from API
            // audio.connecttohost(url);
            Music.start_flag = 0;
            Serial.printf("[Music] Playing: %s\n", Music.song_name);
            ui_show_toast("\xe6\x92\xad\xe6\x94\xbe\xe4\xb8\xad...");
        }

        // Auto next when finished
        if (!audio.isRunning() && !Music.start_flag && Music.pause_flag == false) {
            if (Music.play_mode == 1) {
                // List loop - next song
                Music.play_index++;
                Music.start_flag = 1;
            } else if (Music.play_mode == 2) {
                // Random
                Music.play_index = random(0, 10);
                Music.start_flag = 1;
            }
            // mode 0 = single loop, do nothing
        }
    } else {
        // Pause
        if (!Music.pause_flag && audio.isRunning()) {
            audio.pauseResume();
            Music.pause_flag = true;
            Serial.println("[Music] Paused");
        }
    }
}

// Play a URL directly
void music_play_url(const char* url)
{
    if (url && strlen(url) > 0) {
        audio.connecttohost(url);
        Music.play_flag = 1;
        Music.pause_flag = false;
        Music.start_flag = 0;
        Serial.printf("[Music] Streaming: %s\n", url);
    }
}
