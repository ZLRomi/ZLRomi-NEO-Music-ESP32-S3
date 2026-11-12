#include "icon.h"

#include <math.h>
#include <stdlib.h>

#include "gfx.h"
#include "theme.h"

#define PI 3.14159265358979323846

/* 16x16 的图标在 320x240 上偏细，所有笔画都按 2px 粗画 */
static void dot2(int x, int y, uint16_t c)
{
    gfx_pixel(x, y, c);
    gfx_pixel(x + 1, y, c);
    gfx_pixel(x, y + 1, c);
    gfx_pixel(x + 1, y + 1, c);
}

static void line2(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        dot2(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void fill_circle(int cx, int cy, int r, uint16_t c)
{
    for (int dy = -r; dy <= r; dy++) {
        int w = (int)(sqrt((double)(r * r - dy * dy)) + 0.5);
        for (int dx = -w; dx <= w; dx++) {
            gfx_pixel(cx + dx, cy + dy, c);
        }
    }
}

static void ring(int cx, int cy, int r, uint16_t c)
{
    for (int dy = -r; dy <= r; dy++) {
        int w = (int)(sqrt((double)(r * r - dy * dy)) + 0.5);
        for (int dx = -w; dx <= w; dx++) {
            if (dx * dx + dy * dy >= (r - 1) * (r - 1)) {
                gfx_pixel(cx + dx, cy + dy, c);
            }
        }
    }
}

/** 圆弧：角度制，0° 正右，逆时针为正 */
static void arc(int cx, int cy, int r, double a0, double a1, uint16_t c)
{
    double step = 1.2;
    for (double a = a0; a <= a1; a += step) {
        double rad = a * PI / 180.0;
        dot2((int)(cx + r * cos(rad)), (int)(cy - r * sin(rad)), c);
    }
}

static void fill_tri(int ax, int ay, int bx, int by, int cx, int cy, uint16_t col)
{
    const int px[3] = { ax, bx, cx };
    const int py[3] = { ay, by, cy };
    int miny = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy);
    int maxy = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy);

    for (int y = miny; y <= maxy; y++) {
        int xs[3], n = 0;
        for (int e = 0; e < 3; e++) {
            int x1 = px[e], y1 = py[e];
            int x2 = px[(e + 1) % 3], y2 = py[(e + 1) % 3];
            if ((y1 <= y && y2 > y) || (y2 <= y && y1 > y)) {
                xs[n++] = x1 + (y - y1) * (x2 - x1) / (y2 - y1);
            }
        }
        if (n >= 2) {
            int lo = xs[0] < xs[1] ? xs[0] : xs[1];
            int hi = xs[0] < xs[1] ? xs[1] : xs[0];
            for (int x = lo; x <= hi; x++) {
                gfx_pixel(x, y, col);
            }
        }
    }
}

static void fill_box(int x, int y, int w, int h, uint16_t c)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            gfx_pixel(x + i, y + j, c);
        }
    }
}

void gfx_icon(int x, int y, icon_id_t id, uint16_t color)
{
    switch (id) {
    case ICON_WIFI:
        dot2(x + 7, y + 12, color);
        arc(x + 7, y + 13, 5, 25, 155, color);
        arc(x + 7, y + 13, 9, 30, 150, color);
        arc(x + 7, y + 13, 13, 38, 142, color);
        break;

    case ICON_LOCK:
        arc(x + 7, y + 7, 4, 15, 165, color);      /* 锁梁 */
        fill_box(x + 3, y + 7, 11, 8, color);      /* 锁体 */
        fill_box(x + 6, y + 9, 2, 4, g_theme.bg); /* 锁孔 */
        break;

    case ICON_SOUND:
        fill_tri(x + 2, y + 6, x + 6, y + 6, x + 6, y + 10, color);
        fill_box(x + 2, y + 6, 4, 5, color);
        arc(x + 6, y + 8, 5, -50, 50, color);
        arc(x + 6, y + 8, 8, -40, 40, color);
        break;

    case ICON_SEARCH:
        ring(x + 6, y + 6, 5, color);
        line2(x + 10, y + 10, x + 14, y + 14, color);
        break;

    case ICON_GEAR:
        ring(x + 7, y + 7, 5, color);
        for (int i = 0; i < 8; i++) {
            double a = i * PI / 4.0;
            int x1 = x + 7 + (int)(4.5 * cos(a));
            int y1 = y + 7 - (int)(4.5 * sin(a));
            int x2 = x + 7 + (int)(7.5 * cos(a));
            int y2 = y + 7 - (int)(7.5 * sin(a));
            line2(x1, y1, x2, y2, color);
        }
        break;

    case ICON_DISC:
        ring(x + 7, y + 7, 7, color);
        ring(x + 7, y + 7, 5, color);
        fill_circle(x + 7, y + 7, 2, color);
        break;

    case ICON_PLAY:
        fill_tri(x + 4, y + 3, x + 4, y + 13, x + 13, y + 8, color);
        break;

    case ICON_PAUSE:
        fill_box(x + 4, y + 3, 3, 11, color);
        fill_box(x + 9, y + 3, 3, 11, color);
        break;

    case ICON_PREV:
        fill_tri(x + 8, y + 3, x + 8, y + 13, x + 1, y + 8, color);
        fill_tri(x + 14, y + 3, x + 14, y + 13, x + 7, y + 8, color);
        break;

    case ICON_NEXT:
        fill_tri(x + 2, y + 3, x + 2, y + 13, x + 9, y + 8, color);
        fill_tri(x + 8, y + 3, x + 8, y + 13, x + 15, y + 8, color);
        break;

    case ICON_NOTE:
        fill_box(x + 9, y + 2, 2, 9, color);       /* 符干 */
        fill_box(x + 10, y + 2, 4, 2, color);      /* 符尾 */
        fill_circle(x + 7, y + 11, 3, color);      /* 符头 */
        gfx_pixel(x + 4, y + 12, g_theme.bg);
        gfx_pixel(x + 5, y + 13, g_theme.bg);
        break;

    case ICON_LIST:
        for (int i = 0; i < 3; i++) {
            int yy = y + 3 + i * 5;
            fill_box(x + 2, yy + 1, 2, 2, color);
            fill_box(x + 6, yy, 8, 2, color);
        }
        break;

    case ICON_HOME:
        fill_tri(x + 1, y + 8, x + 8, y + 2, x + 15, y + 8, color);
        fill_box(x + 3, y + 8, 10, 7, color);
        fill_box(x + 7, y + 10, 3, 5, g_theme.bg);
        break;

    case ICON_SUN:
        fill_circle(x + 7, y + 7, 3, color);
        for (int i = 0; i < 8; i++) {
            double a = i * PI / 4.0;
            line2(x + 7 + (int)(5 * cos(a)), y + 7 - (int)(5 * sin(a)),
                  x + 7 + (int)(7 * cos(a)), y + 7 - (int)(7 * sin(a)), color);
        }
        break;

    case ICON_CHECK:
        line2(x + 2, y + 8, x + 6, y + 12, color);
        line2(x + 6, y + 12, x + 13, y + 3, color);
        break;

    case ICON_BACK:
        line2(x + 13, y + 8, x + 3, y + 8, color);
        line2(x + 3, y + 8, x + 9, y + 3, color);
        line2(x + 3, y + 8, x + 9, y + 13, color);
        break;

    default:
        break;
    }
}
