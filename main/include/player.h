/*
 * player.h —— 在线 MP3 播放
 *
 * HTTP 流边下边解，minimp3 软解后送 I2S 给 PCM5102A。
 * 播放模式（顺序/单曲循环/随机）由 UI 管理，这里只负责一首歌的播放，
 * 播完通过回调通知界面切下一首。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "netease.h"

typedef enum {
    PLAYER_IDLE = 0,
    PLAYER_LOADING,
    PLAYER_PLAYING,
    PLAYER_PAUSED,
    PLAYER_ERROR,
} player_state_t;

void player_init(void);
void player_set_end_callback(void (*cb)(void));

bool player_play(const song_t *song, int quality);
void player_toggle(void);
void player_stop(void);
void player_set_volume(int vol);

player_state_t player_state(void);
int  player_position_ms(void);
int  player_duration_ms(void);
int  player_volume(void);
const song_t *player_song(void);
