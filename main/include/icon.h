/*
 * icon.h —— 16x16 图标集
 *
 * 全部用几何图元程序化绘制（圆/弧/三角形/线段），不依赖图标字体，
 * 颜色随主题走，缩放到 16x16 依然清晰。
 */
#pragma once

#include <stdint.h>

typedef enum {
    ICON_WIFI = 0,
    ICON_LOCK,
    ICON_SOUND,
    ICON_SEARCH,
    ICON_GEAR,
    ICON_DISC,
    ICON_PLAY,
    ICON_PAUSE,
    ICON_PREV,
    ICON_NEXT,
    ICON_NOTE,
    ICON_LIST,
    ICON_HOME,
    ICON_SUN,
    ICON_CHECK,
    ICON_BACK,
    ICON_COUNT
} icon_id_t;

/** 在 (x,y) 处画一个 16x16 图标 */
void gfx_icon(int x, int y, icon_id_t id, uint16_t color);
