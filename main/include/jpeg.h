/*
 * jpeg.h —— 轻量 baseline JPEG 解码（封面用）
 *
 * 不引第三方组件，只依赖 IDF 自带libc：支持 baseline / 8bit / Huffman，
 * 4:4:4 与 4:2:0、4:2:2 采样，输出缩放后的 RGB565。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** 成功返回 true；out 大小为 out_w*out_h 个 uint16_t */
bool jpeg_decode_rgb565(const uint8_t *data, size_t len,
                        uint16_t *out, int out_w, int out_h);
