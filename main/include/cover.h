/*
 * cover.h —— 歌曲封面下载与缓存
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define COVER_W  56
#define COVER_H  56

typedef struct {
    uint16_t px[COVER_W * COVER_H];
} cover_t;

/** 取封面：先查内存缓存，未命中才下载 JPEG 并解码。失败返回 false 并生成无图封面 */
bool cover_get(int64_t song_id, const char *url, cover_t *out);
