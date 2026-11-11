/**
 * @file audio_engine.cpp
 * @brief ZLRomi NEO Music Player - 音频引擎实现
 * 
 * 基于 ESP32-audioI2S 库 (schreibfaul1)
 * I2S PCM5102A: BCK=GPIO9, LRCK=GPIO11, DIN=GPIO10, 44100Hz/16bit/stereo
 */

#include "audio_engine.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include "Audio.h"

// ---- ESP32-audioI2S 实例 ----
static Audio* g_audio = nullptr;

// ---- 内部状态 ----
static PlayState s_state = PLAY_STOPPED;
static int s_volume = 15; // 0-21, 默认中等偏大
static bool s_initialized = false;

// ---- 回调 ----
static audio_progress_cb s_progress_cb = nullptr;
static audio_state_cb s_state_cb = nullptr;
static audio_error_cb s_error_cb = nullptr;

// ---- 进度追踪 ----
static unsigned long s_last_progress_time = 0;

// ---- 状态变更辅助 ----
static void set_state(PlayState st) {
    if (s_state != st) {
        s_state = st;
        if (s_state_cb) s_state_cb(st);
    }
}

static void report_error(const char* msg) {
    Serial.printf("[AudioEngine] Error: %s\n", msg);
    if (s_error_cb) s_error_cb(msg);
}

// =====================================================================
//  ESP32-audioI2S 回调函数 (全局C链接)
// =====================================================================

// 流媒体标题信息
void audio_showstreamtitle(const char* info) {
    Serial.printf("[AudioEngine] StreamTitle: %s\n", info);
}

// ID3元数据
void audio_id3data(const char* info) {
    Serial.printf("[AudioEngine] ID3: %s\n", info);
}

// 流结束
void audio_eof_stream(const char* info) {
    Serial.printf("[AudioEngine] EOF: %s\n", info);
    set_state(PLAY_STOPPED);
}

// 音频信息显示
void audio_info(const char* info) {
    Serial.printf("[AudioEngine] Info: %s\n", info);
    // 检测关键信息
    if (strstr(info, "connect to host") != nullptr) {
        set_state(PLAY_BUFFERING);
    } else if (strstr(info, "playing") != nullptr || strstr(info, "codec") != nullptr) {
        set_state(PLAY_PLAYING);
    }
}

// 比特率
void audio_bitrate(const char* info) {
    Serial.printf("[AudioEngine] Bitrate: %s\n", info);
}

// 最后播放的主机
void audio_lasthost(const char* info) {
    Serial.printf("[AudioEngine] LastHost: %s\n", info);
}

// =====================================================================
//  公共API实现
// =====================================================================

void audio_engine_init() {
    if (s_initialized) return;

    Serial.println("[AudioEngine] Initializing...");

    g_audio = new Audio();
    if (!g_audio) {
        report_error("Failed to allocate Audio object");
        return;
    }

    // 设置I2S引脚: BCK, LRCK(WSEL), DIN(DOUT)
    g_audio->setPinout(I2S_BCK_PIN, I2S_LRCK_PIN, I2S_DIN_PIN);

    // 设置初始音量 (0-21)
    g_audio->setVolume(s_volume);

    // 强制采样率 (可选，ESP32-audioI2S会自动检测)
    // g_audio->forceMono(false);

    s_initialized = true;
    s_state = PLAY_STOPPED;

    Serial.printf("[AudioEngine] Initialized: BCK=%d, LRCK=%d, DIN=%d, vol=%d/21\n",
                  I2S_BCK_PIN, I2S_LRCK_PIN, I2S_DIN_PIN, s_volume);
}

bool audio_play_url(const char* url) {
    if (!s_initialized || !g_audio) {
        report_error("Audio not initialized");
        return false;
    }
    if (!url || strlen(url) == 0) {
        report_error("Empty URL");
        return false;
    }

    Serial.printf("[AudioEngine] Playing: %s\n", url);
    set_state(PLAY_BUFFERING);

    // ESP32-audioI2S connecttohost 支持HTTP/HTTPS流
    bool ok = g_audio->connecttohost(url);
    if (!ok) {
        report_error("connecttohost failed");
        set_state(PLAY_ERROR);
        return false;
    }

    return true;
}

void audio_pause() {
    if (!s_initialized || !g_audio) return;
    if (s_state == PLAY_PLAYING) {
        g_audio->pauseResume();
        set_state(PLAY_PAUSED);
        Serial.println("[AudioEngine] Paused");
    }
}

void audio_resume() {
    if (!s_initialized || !g_audio) return;
    if (s_state == PLAY_PAUSED) {
        g_audio->pauseResume();
        set_state(PLAY_PLAYING);
        Serial.println("[AudioEngine] Resumed");
    }
}

void audio_stop() {
    if (!s_initialized || !g_audio) return;
    g_audio->stopSong();
    set_state(PLAY_STOPPED);
    Serial.println("[AudioEngine] Stopped");
}

void audio_set_volume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 21) vol = 21;
    s_volume = vol;
    if (s_initialized && g_audio) {
        g_audio->setVolume(vol);
    }
    Serial.printf("[AudioEngine] Volume: %d/21\n", vol);
}

int audio_get_volume() {
    return s_volume;
}

PlayState audio_get_state() {
    return s_state;
}

int audio_get_position_ms() {
    if (!s_initialized || !g_audio) return 0;
    // getAudioCurrentTime() 返回秒
    return (int)(g_audio->getAudioCurrentTime() * 1000);
}

int audio_get_duration_ms() {
    if (!s_initialized || !g_audio) return 0;
    // getAudioFileDuration() 返回秒
    return (int)(g_audio->getAudioFileDuration() * 1000);
}

bool audio_is_running() {
    if (!s_initialized || !g_audio) return false;
    return g_audio->isRunning();
}

void audio_task() {
    if (!s_initialized || !g_audio) return;

    // ESP32-audioI2S 核心解码循环 - 必须高频调用
    g_audio->loop();

    // 定期上报进度 (每500ms)
    unsigned long now = millis();
    if (now - s_last_progress_time >= 500) {
        s_last_progress_time = now;
        if (s_state == PLAY_PLAYING && s_progress_cb) {
            int pos = audio_get_position_ms();
            int dur = audio_get_duration_ms();
            s_progress_cb(pos, dur);
        }
    }
}

void audio_set_progress_callback(audio_progress_cb cb) {
    s_progress_cb = cb;
}

void audio_set_state_callback(audio_state_cb cb) {
    s_state_cb = cb;
}

void audio_set_error_callback(audio_error_cb cb) {
    s_error_cb = cb;
}