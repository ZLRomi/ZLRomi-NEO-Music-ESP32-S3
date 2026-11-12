/*
 * theme.h —— 深色主题配色
 *
 * 黑胶唱片的暖调：近黑底 + 米白字 + 琥珀点缀。
 * 不用蓝紫渐变、不用发光卡片，避免那种一眼就能看出的「AI 味」。
 */
#pragma once

#include <stdint.h>

#define C565(r, g, b)  ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

typedef struct {
    uint16_t bg;          /* 页面底色 */
    uint16_t surface;     /* 面板底色 */
    uint16_t surface_hi;  /* 抬起的面板 */
    uint16_t text;        /* 主文字 */
    uint16_t text_dim;    /* 次要文字 */
    uint16_t accent;      /* 琥珀色强调 */
    uint16_t accent_dim;  /* 暗琥珀 */
    uint16_t line;        /* 分隔线 */
    uint16_t focus;       /* 焦点条底色 */
    uint16_t focus_edge;  /* 焦点条左侧竖线 */
    uint16_t ok;
    uint16_t warn;
} theme_t;

extern const theme_t g_theme;
