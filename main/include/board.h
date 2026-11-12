/*
 * board.h —— HW-678 硬件引脚单一真相
 *
 * 所有 GPIO 只在这里定义一次，任何模块都不许自己写数字。
 * 依据：排线丝印 + 会话中反复实测确认的接线。
 */
#pragma once

/* ── 屏幕：ST7789 物理 240x320 竖屏，4 线 SPI，BGR 颜色顺序 ────
   排线丝印: GND VCC SCL SDA RES DC  CS  BLK
   对应 GPIO:GND 3V3 G21 G47 G45 G40 G41 G42
   ⚠️ 整机按横屏使用，逻辑分辨率是 320x240（见 gfx.h），
      物理尺寸仍为 240x320，靠 MADCTL 的 MV 位转置。          */
#define LCD_SCLK        21
#define LCD_MOSI        47
#define LCD_RST         45
#define LCD_DC          40
#define LCD_CS          41
#define LCD_BL          42
#define LCD_W           240
#define LCD_H           320
#define LCD_BGR         1
#define LCD_SPI_HOST    SPI3_HOST
#define LCD_SPI_MHZ     80

/* ── 音频：PCM5102A I2S DAC（有线输出）──────────────────────
   SCK 接 GND，用内部 PLL，mclk 必须 I2S_GPIO_UNUSED。
   ESP32-S3 无经典蓝牙 BR/EDR，A2DP 物理不可行，不要再引入蓝牙音频。 */
#define I2S_BCK          9
#define I2S_LRCK        11
#define I2S_DIN         10
#define I2S_PORT        I2S_NUM_0

/* ── 7 键：内部上拉，按下为低电平，无触摸屏 ─────────────────── */
#define KEY_PIN_UP      17
#define KEY_PIN_DOWN    16
#define KEY_PIN_LEFT    15
#define KEY_PIN_RIGHT    4
#define KEY_PIN_OK       5
#define KEY_PIN_SET      6
#define KEY_PIN_RST      7

/* ── USB OTG：接 U 盘（D+/D- 由 USB 外设固定占用）───────────── */
#define USB_DP          19
#define USB_DM          20

/* ── 已被芯片占用、不可再分配的脚 ─────────────────────────────
   OPI PSRAM : 33~37
   Flash     : 26~32
   Strapping : 0 / 3 / 45 / 46（45 已用作屏幕 RST，实测可用）
   UART0     : 43 / 44（控制台走 USB-CDC，故空闲）           */

#define FW_VERSION      "1.0.0"
