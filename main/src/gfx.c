#include "gfx.h"

#include <string.h>

#include "board.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "font_data.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "theme.h"

static const char *TAG = "gfx";

/* 初始化里任何一步失败只记日志并返回，绝不 abort：
   一处外设没起来不该让整机陷入无限重启，UI 与按键仍要能用。 */
#define TRY(expr) do {                                                  \
    esp_err_t e_ = (expr);                                              \
    if (e_ != ESP_OK) {                                                 \
        ESP_LOGE(TAG, "%s 失败: %s", #expr, esp_err_to_name(e_));      \
        return;                                                         \
    }                                                                   \
} while (0)

#define FLUSH_ROWS  20      /* 每次 DMA 送 20 行，中转缓冲 320*20*2 = 12.5KB 内部 RAM */

static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t      s_flush_done;
static uint16_t              *s_fb;         /* PSRAM 里的整帧缓冲 */
static uint16_t              *s_dma;        /* 内部 RAM 的刷屏中转 */
static EXT_RAM_BSS_ATTR uint16_t s_line[GFX_W * FONT_H];
static int                    s_dirty_y0 = GFX_H;
static int                    s_dirty_y1 = 0;
static int                    s_offset_y;
static int                    s_bright = 80;
static bool                   s_bl_on = true;

/* SPI 以小端发 uint16_t，ST7789 要大端，必须换字节 */
static inline uint16_t to_panel(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}

static void mark_dirty(int y0, int y1)
{
    if (y0 < 0) y0 = 0;
    if (y1 > GFX_H) y1 = GFX_H;
    if (y0 < s_dirty_y0) s_dirty_y0 = y0;
    if (y1 > s_dirty_y1) s_dirty_y1 = y1;
}

static void apply_backlight(void)
{
    uint32_t duty = s_bl_on ? (10u + (uint32_t)s_bright * 245u / 100u) : 0u;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static bool on_color_done(esp_lcd_panel_io_handle_t io,
                          esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    BaseType_t woken = pdFALSE;
    if (s_flush_done) {
        xSemaphoreGiveFromISR(s_flush_done, &woken);
    }
    return woken == pdTRUE;
}

static void wait_flush(void)
{
    if (s_flush_done) {
        xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(200));
    }
}

void gfx_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK,
    };
    TRY(ledc_timer_config(&timer));
    ledc_channel_config_t ch = {
        .gpio_num = LCD_BL, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0,
        .duty = 0, .hpoint = 0,
    };
    TRY(ledc_channel_config(&ch));

    s_flush_done = xSemaphoreCreateBinary();
    if (s_flush_done) xSemaphoreGive(s_flush_done);

    /* 整帧 320*240*2 = 150KB 放 PSRAM：启动时内部堆只有约 137KB，
       装不下这 150KB，还会把 WiFi / I2S 的 DMA 缓冲挤到 ESP_ERR_NO_MEM。
       SPI DMA 不能直接读 PSRAM，刷屏时由 s_dma 分块中转。 */
    s_fb = heap_caps_malloc((size_t)GFX_W * GFX_H * sizeof(uint16_t),
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_dma = heap_caps_malloc((size_t)GFX_W * FLUSH_ROWS * sizeof(uint16_t),
                             MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!s_fb || !s_dma) {
        ESP_LOGE(TAG, "帧缓冲分配失败（PSRAM 150KB + 内部 DMA 12.5KB）");
        return;
    }
    memset(s_fb, 0, (size_t)GFX_W * GFX_H * sizeof(uint16_t));

    spi_bus_config_t bus = {
        .sclk_io_num = LCD_SCLK, .mosi_io_num = LCD_MOSI, .miso_io_num = -1,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = GFX_W * GFX_H * sizeof(uint16_t),
    };
    TRY(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = LCD_DC,
        .cs_gpio_num = LCD_CS,          /* HW-678 有独立 CS=GPIO41 */
        .pclk_hz = LCD_SPI_MHZ * 1000 * 1000,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .spi_mode = 3, .trans_queue_depth = 10,
        .on_color_trans_done = on_color_done, .user_ctx = NULL,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    TRY(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                                            &io_cfg, &io));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,   /* MADCTL BGR 位，红蓝不反 */
        .bits_per_pixel = 16,
    };
    TRY(esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel));
    TRY(esp_lcd_panel_reset(s_panel));
    TRY(esp_lcd_panel_init(s_panel));
    TRY(esp_lcd_panel_invert_color(s_panel, true));
    TRY(esp_lcd_panel_disp_on_off(s_panel, true));

    /* 整机横屏：默认方向 0 就是横屏，MV 位由 gfx_set_rotation 打开 */
    gfx_set_rotation(0);

    s_bl_on = true;
    apply_backlight();
    mark_dirty(0, GFX_H);
    ESP_LOGI(TAG, "ST7789 横屏 320x240 就绪，CS=GPIO%d，SPI %dMHz", LCD_CS, LCD_SPI_MHZ);
}

void gfx_flush(void)
{
    if (!s_panel || !s_fb || !s_dma || s_dirty_y0 >= s_dirty_y1) {
        return;
    }
    int y0 = s_dirty_y0, y1 = s_dirty_y1;
    s_dirty_y0 = GFX_H;
    s_dirty_y1 = 0;

    /* 每块先等上一笔 DMA 完成，再把 PSRAM 里的像素拷进内部中转缓冲 */
    for (int y = y0; y < y1; y += FLUSH_ROWS) {
        int rows = (y + FLUSH_ROWS <= y1) ? FLUSH_ROWS : (y1 - y);
        wait_flush();
        memcpy(s_dma, &s_fb[(size_t)y * GFX_W],
               (size_t)GFX_W * rows * sizeof(uint16_t));
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, GFX_W, y + rows, s_dma);
    }
}

void gfx_fill(uint16_t color)
{
    if (!s_fb) return;
    uint16_t pc = to_panel(color);
    for (int i = 0; i < GFX_W * GFX_H; i++) s_fb[i] = pc;
    mark_dirty(0, GFX_H);
}

void gfx_rect(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (!s_fb) return;
    y0 += s_offset_y;
    y1 += s_offset_y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > GFX_W) x1 = GFX_W;
    if (y1 > GFX_H) y1 = GFX_H;
    if (x0 >= x1 || y0 >= y1) return;
    uint16_t pc = to_panel(color);
    for (int y = y0; y < y1; y++) {
        uint16_t *row = &s_fb[(size_t)y * GFX_W];
        for (int x = x0; x < x1; x++) row[x] = pc;
    }
    mark_dirty(y0, y1);
}

void gfx_hline(int x0, int x1, int y, uint16_t color)
{
    gfx_rect(x0, y, x1, y + 1, color);
}

void gfx_vline(int x, int y0, int y1, uint16_t color)
{
    gfx_rect(x, y0, x + 1, y1, color);
}

void gfx_frame(int x0, int y0, int x1, int y1, uint16_t color)
{
    gfx_rect(x0, y0, x1, y0 + 1, color);
    gfx_rect(x0, y1 - 1, x1, y1, color);
    gfx_rect(x0, y0, x0 + 1, y1, color);
    gfx_rect(x1 - 1, y0, x1, y1, color);
}

void gfx_round_rect(int x, int y, int w, int h, int r, uint16_t color)
{
    static EXT_RAM_BSS_ATTR uint16_t buf[GFX_W * 32];
    if (!s_fb || w <= 0 || h <= 0) return;
    if (w > GFX_W || h > 32 || r <= 0) {
        gfx_rect(x, y, x + w, y + h, color);
        return;
    }
    for (int ry = 0; ry < h; ry++) {
        int dx = 0;
        if (ry < r) {
            int dy = r - ry;
            int rr = r * r - dy * dy;
            int root = 0;
            while ((root + 1) * (root + 1) <= rr) root++;
            dx = r - root;
        } else if (ry >= h - r) {
            int dy = ry - (h - r - 1);
            int rr = r * r - dy * dy;
            int root = 0;
            while ((root + 1) * (root + 1) <= rr) root++;
            dx = r - root;
        }
        for (int rx = 0; rx < w; rx++) {
            buf[ry * w + rx] = (rx >= dx && rx < w - dx) ? to_panel(color) : 0;
        }
    }
    /* 圆角外透明：这里不做透明混合，直接贴整块，调用方负责先铺背景 */
    for (int ry = 0; ry < h; ry++) {
        memcpy(&s_fb[(size_t)(y + s_offset_y + ry) * GFX_W + x],
               &buf[(size_t)ry * w], (size_t)w * sizeof(uint16_t));
    }
    mark_dirty(y + s_offset_y, y + s_offset_y + h);
}

void gfx_blit(int x, int y, int w, int h, const uint16_t *buf)
{
    if (!s_fb || !buf || w <= 0 || h <= 0) return;
    y += s_offset_y;
    if (x < 0 || y < 0 || x + w > GFX_W || y + h > GFX_H) return;
    for (int ry = 0; ry < h; ry++) {
        uint16_t *dst = &s_fb[(size_t)(y + ry) * GFX_W + x];
        const uint16_t *src = &buf[(size_t)ry * w];
        for (int rx = 0; rx < w; rx++) dst[rx] = to_panel(src[rx]);
    }
    mark_dirty(y, y + h);
}

void gfx_blit_scaled(int x, int y, int w, int h,
                     const uint16_t *src, int sw, int sh)
{
    if (!s_fb || !src || w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return;
    y += s_offset_y;
    if (x < 0 || y < 0 || x + w > GFX_W || y + h > GFX_H) return;
    for (int dy = 0; dy < h; dy++) {
        int sy = dy * sh / h;
        uint16_t *dst = &s_fb[(size_t)(y + dy) * GFX_W + x];
        for (int dx = 0; dx < w; dx++) {
            int sx = dx * sw / w;
            dst[dx] = to_panel(src[(size_t)sy * sw + sx]);
        }
    }
    mark_dirty(y, y + h);
}

void gfx_pixel(int x, int y, uint16_t color)
{
    if (!s_fb) return;
    y += s_offset_y;
    if (x < 0 || y < 0 || x >= GFX_W || y >= GFX_H) return;
    s_fb[(size_t)y * GFX_W + x] = to_panel(color);
    mark_dirty(y, y + 1);
}

int gfx_text_width(const char *s)
{
    return s ? font_text_width(s) : 0;
}

static inline uint16_t blend565(uint16_t bg, uint16_t fg, unsigned a)
{
    /* 必须带符号：浅色主题下 fg 比 bg 暗，减法会出负数 */
    int br = (bg >> 11) & 0x1F, bcg = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    int fr = (fg >> 11) & 0x1F, fcg = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    int r = br + ((fr - br) * (int)a) / 15;
    int g = bcg + ((fcg - bcg) * (int)a) / 15;
    int b = bb + ((fb - bb) * (int)a) / 15;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* 一套字体的参数：汉字宽高、ASCII 宽、两种字形的行字节数 */
typedef struct {
    int cw, ch;          /* 汉字宽 / 高 */
    int aw;              /* ASCII 显示宽 */
    int cstride;         /* 汉字字形每行字节 */
    int astride;         /* ASCII 字形每行字节 */
    const uint8_t *(*cn)(uint32_t cp);
    const uint8_t *(*asc)(int c);
} fontset_t;

static const fontset_t FS_BIG = {
    FONT_W, FONT_H, FONT_ASCII_W, FONT_W / 2, FONT_W / 2, font_lookup, font_ascii
};
static const fontset_t FS_SMALL = {
    FONT16_W, FONT16_H, FONT16_ASCII_W, FONT16_W / 2, (FONT16_ASCII_W + 1) / 2,
    font_lookup16, font_ascii16
};

static int draw_text(int x, int y, const char *s, uint16_t fg, uint16_t bg,
                     const fontset_t *fs)
{
    if (!s_fb || !s) return 0;
    y += s_offset_y;
    int total = (fs == &FS_SMALL) ? font_text_width16(s) : font_text_width(s);
    if (total <= 0) return 0;
    if (y < 0 || y + fs->ch > GFX_H) return total;

    int vx0 = x < 0 ? 0 : x;
    int vx1 = x + total > GFX_W ? GFX_W : x + total;
    int w = vx1 - vx0;
    if (w <= 0) return total;

    uint16_t pb = to_panel(bg);
    for (int i = 0; i < w * fs->ch; i++) s_line[i] = pb;

    int cx = x;
    for (const char *p = s; *p;) {
        int n = 1;
        uint32_t cp = font_utf8_next(p, &n);
        const uint8_t *g;
        int gw, stride;

        if (cp < 0x80) {
            g      = fs->asc((int)cp);
            gw     = fs->aw;
            stride = fs->astride;
        } else {
            g      = fs->cn(cp);
            gw     = fs->cw;
            stride = fs->cstride;
        }

        int gx0 = (cx < vx0) ? (vx0 - cx) : 0;
        int gx1 = (cx + gw > vx1) ? (vx1 - cx) : gw;
        int dx = cx - vx0;

        for (int ry = 0; ry < fs->ch; ry++) {
            if (!g) {
                /* 字库缺字：画空心方块，一眼能看出缺哪个字 */
                if (gx0 <= 0 && gx1 >= gw && (ry == 0 || ry == fs->ch - 1)) {
                    for (int rx = 0; rx < gw; rx++)
                        s_line[ry * w + dx + rx] = to_panel(fg);
                } else if (ry > 0 && ry < fs->ch - 1) {
                    int rx = (gx0 <= 0) ? 0 : gw - 1;
                    if (rx >= gx0 && rx < gx1)
                        s_line[ry * w + dx + rx] = to_panel(fg);
                }
                continue;
            }
            const uint8_t *row = g + ry * stride;
            for (int rx = gx0; rx < gx1; rx++) {
                uint8_t b = row[rx >> 1];
                unsigned a = (rx & 1) ? (b & 0x0F) : (b >> 4);
                if (a) s_line[ry * w + dx + rx] = to_panel(blend565(bg, fg, a));
            }
        }
        cx += gw;
        p += n;
    }

    for (int ry = 0; ry < fs->ch; ry++) {
        memcpy(&s_fb[(size_t)(y + ry) * GFX_W + vx0],
               &s_line[(size_t)ry * w], (size_t)w * sizeof(uint16_t));
    }
    mark_dirty(y, y + fs->ch);
    return total;
}

int gfx_text(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    return draw_text(x, y, s, fg, bg, &FS_BIG);
}

int gfx_text_small(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    return draw_text(x, y, s, fg, bg, &FS_SMALL);
}

int gfx_text_width_small(const char *s)
{
    return s ? font_text_width16(s) : 0;
}

int gfx_text_center_small(int y, const char *s, uint16_t fg, uint16_t bg)
{
    int tw = gfx_text_width_small(s);
    int x = (GFX_W - tw) / 2;
    gfx_text_small(x, y, s, fg, bg);
    return x;
}

int gfx_text_center(int y, const char *s, uint16_t fg, uint16_t bg)
{
    int tw = gfx_text_width(s);
    int x = (GFX_W - tw) / 2;
    gfx_text(x, y, s, fg, bg);
    return x;
}

void gfx_focus_row(int x, int y, int w, int h, bool focused, uint16_t bg)
{
    if (focused) {
        gfx_rect(x, y, x + w, y + h, g_theme.focus);
        gfx_rect(x, y, x + 3, y + h, g_theme.focus_edge);
    } else {
        gfx_rect(x, y, x + w, y + h, bg);
    }
}

/*
 * 默认横屏。0/1 是两种横屏方向（上下颠倒时切到 1），2/3 是竖屏。
 * MV=1 时控制器把行列转置，draw_bitmap 仍按逻辑 320x240 送数据。
 */
void gfx_set_rotation(int mode)
{
    if (!s_panel) return;
    bool swap = true, mx = false, my = false;
    switch (mode & 3) {
    case 0: swap = true;  mx = true;  my = false; break;   /* 横屏（默认，实测方向） */
    case 1: swap = true;  mx = false; my = true;  break;   /* 横屏翻转 180° */
    case 2: swap = false; mx = false; my = false; break;   /* 竖屏 */
    case 3: swap = false; mx = true;  my = true;  break;   /* 竖屏翻转 */
    }
    wait_flush();
    esp_lcd_panel_swap_xy(s_panel, swap);
    esp_lcd_panel_mirror(s_panel, mx, my);
}

void gfx_offset_y(int dy)
{
    s_offset_y = dy;
}

void gfx_brightness(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_bright = pct;
    apply_backlight();
}

void gfx_sleep(void)
{
    if (!s_panel) return;
    gfx_fill(0);
    gfx_flush();
    s_bl_on = false;
    apply_backlight();
    esp_lcd_panel_disp_on_off(s_panel, false);
}

void gfx_wake(void)
{
    if (!s_panel) return;
    esp_lcd_panel_disp_on_off(s_panel, true);
    s_bl_on = true;
    apply_backlight();
}
