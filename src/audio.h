#pragma once
#include <stddef.h>
#include "types.h"

// 初始化音频系统(I2S PCM5102A + BT A2DP)
void audio_init();

// 写入PCM数据到两路输出
void audio_write(const uint8_t* data, size_t len);

// 音量 0-100
void audio_set_volume(int vol);
int audio_get_volume();

// 播放HTTP URL音频流
bool audio_play_url(const char* url);

// 播放控制
void audio_pause();
void audio_resume();
void audio_stop();

// 状态查询
PlayState audio_get_state();
int audio_get_position_ms();
int audio_get_duration_ms();

// 音质
void audio_set_quality(AudioQuality q);
AudioQuality audio_get_quality();

// 后台任务(流媒体下载+解码)
void audio_task();

// 回调
typedef void (*audio_progress_cb)(int pos_ms, int dur_ms);
typedef void (*audio_state_cb)(PlayState state);
typedef void (*audio_error_cb)(const char* msg);

void audio_set_progress_callback(audio_progress_cb cb);
void audio_set_state_callback(audio_state_cb cb);
void audio_set_error_callback(audio_error_cb cb);
