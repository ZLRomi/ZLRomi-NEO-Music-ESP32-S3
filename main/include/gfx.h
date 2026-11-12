/*
 * gfx.h —— 横屏 320x240 显示与绘图
 *
 * 物理屏是 240x320 竖屏，但整机按横屏使用：MADCTL 打开 MV 转置后，
 * 逻辑坐标就是 320 宽 x 240 高。所有界面一律按横屏排版。
 * 所有绘制先写内存整帧缓冲，gfx_flush() 一次性推给屏幕，
 * 避免从上往下一条条刷出来。文字走工程自带的 GB2312 点阵字库。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GFX_W  320
#define GFX_H  240

void gfx_init(void);
void gfx_flush(void);

void gfx_fill(uint16_t color);
void gfx_rect(int x0, int y0, int x1, int y1, uint16_t color);
void gfx_round_rect(int x, int y, int w, int h, int r, uint16_t color);
void gfx_hline(int x0, int x1, int y, uint16_t color);
void gfx_vline(int x, int y0, int y1, uint16_t color);
void gfx_frame(int x0, int y0, int x1, int y1, uint16_t color);
void gfx_blit(int x, int y, int w, int h, const uint16_t *buf);
/** 最近邻缩放贴图：封面从 56x56 缩到列表行的 40x40 */
void gfx_blit_scaled(int x, int y, int w, int h,
                     const uint16_t *src, int sw, int sh);

int  gfx_text(int x, int y, const char *s, uint16_t fg, uint16_t bg);
int  gfx_text_width(const char *s);
int  gfx_text_center(int y, const char *s, uint16_t fg, uint16_t bg);

/** 焦点条：左侧琥珀竖线 + 抬起的底色，电视遥控风格 */
void gfx_focus_row(int x, int y, int w, int h, bool focused, uint16_t bg);

void gfx_set_rotation(int mode);
void gfx_offset_y(int dy);
void gfx_brightness(int pct);
void gfx_sleep(void);
void gfx_wake(void);
