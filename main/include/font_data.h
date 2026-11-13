/*
 * font_data —— 4bpp 抗锯齿点阵字库（自动生成，请勿手动修改）
 *
 *   中文：微软雅黑 20px，GB2312 全部 6763 字，20×24 格
 *   ASCII：Consolas 20px，95 个可打印字符，占左侧 11px
 *
 * 每个字形 240 字节（20 宽 × 24 高，4bpp，每行 10 字节，高 nibble 是左像素）。
 * 字形数据放在独立 .bin 里（EMBED_FILES 嵌入 flash），避免生成几百 KB 的 .c。
 *
 * 重新生成：_work/gen_font.py
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FONT_W            20
#define FONT_H            24
#define FONT_ASCII_W      11
#define FONT_GLYPH_BYTES  (FONT_W * FONT_H / 2)   /* 240 */

/* ── 小号字库 16x16 ──────────────────────────────────────────
   大字一行只放得下 16 个汉字，歌名/歌手这类长文本显示不下；
   小字一行能放 20 个汉字，行高也从 24 降到 16。
   由 scripts/gen_font16.py 生成，码位表与上面的大字库完全一致。 */
#define FONT16_W           16
#define FONT16_H           16
#define FONT16_ASCII_W     9
#define FONT16_CN_BYTES    (FONT16_W * FONT16_H / 2)                  /* 128 */
#define FONT16_ASCII_BYTES (((FONT16_ASCII_W + 1) / 2) * FONT16_H)  /* 80 */

/** @brief GB2312 汉字码位表（升序），下标即 font_cn.bin 字形序号 */
extern const uint16_t g_cn_codepoints[];
extern const int      g_cn_codepoints_count;

/** @brief 小字库专用码位表（GB2312 + 常用标点，升序），下标即 font_cn16.bin 字形序号 */
extern const uint16_t g_cn16_codepoints[];
extern const int      g_cn16_codepoints_count;

/**
 * @brief 按 Unicode 码位取字形
 * @return 144 字节字形数据；字库没有该字时返回 NULL
 */
const uint8_t *font_lookup(uint32_t cp);

/** @brief 取 ASCII 字形（c 必须在 32~126） */
const uint8_t *font_ascii(int c);

/* ── 小号字库（16x16）对应接口 ─────────────────────────────── */

/** @brief 按 Unicode 码位取 16x16 字形；字库没有该字时返回 NULL */
const uint8_t *font_lookup16(uint32_t cp);

/** @brief 取 16x16 的 ASCII 字形（c 必须在 32~126） */
const uint8_t *font_ascii16(int c);

/** @brief 一段文本在小字下占多少像素宽 */
int font_text_width16(const char *s);

/**
 * @brief 解码一个 UTF-8 字符
 * @param s    字符串指针
 * @param len  输出：该字符占几个字节
 * @return 码位；遇到非法字节返回 0xFFFD
 */
uint32_t font_utf8_next(const char *s, int *len);

/** @brief 一段文本占多少像素宽（不含行距） */
int font_text_width(const char *s);

#ifdef __cplusplus
}
#endif
