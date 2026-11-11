/**
 * @file audio_engine.h
 * @brief ZLRomi NEO Music Player - 音频引擎(ESP32-audioI2S封装)
 * 
 * I2S PCM5102A: BCK=GPIO9, LRCK=GPIO11, DIN=GPIO10
 * 使用 ESP32-audioI2S 库进行HTTP流媒体解码播放
 */
#pragma once
#include <stddef.h>
#include "types.h"

// I2S引脚定义
#define I2S_BCK_PIN   9
#define I2S_LRCK_PIN  11
#define I2S_DIN_PIN   10

// 初始化音频引擎(I2S + ESP32-audioI2S)
void audio_engine_init();

// 播放HTTP音频流URL
bool audio_play_url(const char* url);

// 播放控制
void audio_pause();
void audio_resume();
void audio_stop();

// 音量 0-21 (ESP32-audioI2S原生范围)
void audio_set_volume(int vol);
int audio_get_volume();

// 状态查询
PlayState audio_get_state();
int audio_get_position_ms();
int audio_get_duration_ms();
bool audio_is_running();

// 后台任务(必须在loop中调用)
void audio_task();

// 回调类型
typedef void (*audio_progress_cb)(int pos_ms, int dur_ms);
typedef void (*audio_state_cb)(PlayState state);
typedef void (*audio_error_cb)(const char* msg);

void audio_set_progress_callback(audio_progress_cb cb);
void audio_set_state_callback(audio_state_cb cb);
void audio_set_error_callback(audio_error_cb cb);
