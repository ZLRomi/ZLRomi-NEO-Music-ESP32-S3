/**
 * @file lv_conf.h
 * ZLRomi NEO Music Player - LVGL v9 Configuration
 * ESP32-S3 + ST7789 240x320 + HW-678
 */

#if 1

#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16

/*=========================
   STDLIB WRAPPER SETTINGS
 *=========================*/
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DEF_REFR_PERIOD  16
#define LV_DPI_DEF 160

/*=================
 * OPERATING SYSTEM
 *=================*/
#define LV_USE_OS   LV_OS_NONE

/*=======================
 * FEATURE CONFIGURATION
 *=======================*/
#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 1
#endif

#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/*==================
 * FONT USAGE
 *==================*/
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_SIMSUN_16_CJK 1
#define LV_FONT_SOURCE_HAN_SANS_SC_14_CJK 1
#define LV_FONT_SOURCE_HAN_SANS_SC_16_CJK 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*=================
 * TEXT ENCODING
 *=================*/
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_"

/*==================
 * WIDGETS
 *==================*/
#define LV_USE_ANIMIMG    1
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BUTTON     1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMAGE      1
#define LV_USE_LABEL      1
#if LV_USE_LABEL
    #define LV_LABEL_TEXT_SELECTION 1
    #define LV_LABEL_LONG_TXT_HINT 1
#endif
#define LV_USE_LINE       1
#define LV_USE_LIST       1
#define LV_USE_MSGBOX     1
#define LV_USE_ROLLER     1
#define LV_USE_SLIDER     1
#define LV_USE_SPINNER    1
#define LV_USE_SWITCH     1
#define LV_USE_TEXTAREA   1
#define LV_USE_KEYBOARD   1
#define LV_USE_TABLE      1
#define LV_USE_TABVIEW    1
#define LV_USE_TILEVIEW   1

/* Pinyin IME */
#define LV_USE_IME_PINYIN 1
#if LV_USE_IME_PINYIN
    #define LV_IME_PINYIN_K9_MAX_INPUT 7
#endif

/*==================
 * THEMES
 *==================*/
#define LV_USE_THEME_SIMPLE 1
#define LV_USE_THEME_DEFAULT 1

/*==================
 * LAYOUTS
 *==================*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*==================
 * DRAW
 *==================*/
#define LV_USE_DRAW_SW 1
#if LV_USE_DRAW_SW
    #define LV_DRAW_SW_SUPPORT_RGB565 1
    #define LV_DRAW_SW_COMPLEX 1
#endif

/*==================
 * TINY TTF (load OTF/TTF fonts at runtime)
 *==================*/
#define LV_USE_TINY_TTF 1
#if LV_USE_TINY_TTF
    #define LV_TINY_TTF_CACHE_GLYPH_CNT 256
    #define LV_TINY_TTF_FILE_SUPPORT 1
#endif

/*==================
 * FILE SYSTEM (for loading fonts from LittleFS)
 *==================*/
#define LV_USE_FS_POSIX 1
#if LV_USE_FS_POSIX
    #define LV_FS_POSIX_LETTER 'A'
    #define LV_FS_POSIX_PATH "/littlefs"
    #define LV_FS_POSIX_CACHE_SIZE 0
#endif

/*==================
 * DISPLAY DRIVERS
 *==================*/
#define LV_USE_ST7789 1
#define LV_USE_GENERIC_MIPI 1
#define LV_USE_TFT_ESPI 1

/*==================
 * IMAGE DECODERS
 *==================*/
#define LV_USE_LIBPNG 0
#define LV_USE_BMP 0
#define LV_USE_SJPG 0
#define LV_USE_GIF 1
#define LV_USE_QRCODE 0

/*==================
 * DEMOS (disabled)
 *==================*/
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_MUSIC 0

#endif /*LV_CONF_H*/
#endif
