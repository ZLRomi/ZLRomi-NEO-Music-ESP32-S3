/**
 * @file main.cpp
 * @brief ZLRomi NEO Music Player - Main Entry
 * Based on Lee-Stone/esp32s3-lvgl-terminal, adapted for HW-678
 * LVGL v9 + ST7789 240x320 + 7-key navigation + PCM5102A I2S
 */
#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <FS.h>

#include "config.h"
#include "ui.h"
#include "wifiuser.h"
#include "data.h"
#include "sduser.h"
#include "music.h"
#include "task.h"
#include "bt_a2dp.h"

// =====================================================================
//  Hardware Pins (HW-678)
// =====================================================================
#define PIN_LCD_BL    42
#define SCREEN_W      240
#define SCREEN_H      320

// Button pins
#define BTN_UP_PIN    17
#define BTN_DOWN_PIN  16
#define BTN_LEFT_PIN  15
#define BTN_RIGHT_PIN  4
#define BTN_OK_PIN     5
#define BTN_SET_PIN    6
#define BTN_RST_PIN    7

// =====================================================================
//  Globals
// =====================================================================
static TFT_eSPI tft;
static const int btn_pins[] = {BTN_UP_PIN, BTN_DOWN_PIN, BTN_LEFT_PIN, BTN_RIGHT_PIN, BTN_OK_PIN, BTN_SET_PIN, BTN_RST_PIN};
static bool btn_last[7] = {true, true, true, true, true, true, true};

// =====================================================================
//  LVGL Display Flush (v9 API)
// =====================================================================
static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)px_map, w * h, true);
    tft.endWrite();

    lv_display_flush_ready(disp);
}

// =====================================================================
//  7-Key Keypad Input (LVGL v9)
// =====================================================================
static void keypad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    static uint32_t last_key = 0;
    data->state = LV_INDEV_STATE_RELEASED;

    for (int i = 0; i < 7; i++) {
        bool pressed = (digitalRead(btn_pins[i]) == LOW);
        if (pressed && btn_last[i]) {
            // New press
            data->state = LV_INDEV_STATE_PRESSED;
            switch (i) {
                case 0: data->key = LV_KEY_UP; break;
                case 1: data->key = LV_KEY_DOWN; break;
                case 2: data->key = LV_KEY_LEFT; break;
                case 3: data->key = LV_KEY_RIGHT; break;
                case 4: data->key = LV_KEY_ENTER; break;
                case 5: data->key = LV_KEY_ESC; break;
                case 6: data->key = LV_KEY_ESC; break;
            }
            last_key = data->key;
            btn_last[i] = false;
            return;
        }
        if (!pressed) {
            btn_last[i] = true;
        }
    }
    data->key = last_key;
}

// =====================================================================
//  LVGL Setup (v9 API)
// =====================================================================
static void lvgl_setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== ZLRomi NEO Music Player ===");

    // Backlight
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, LOW);

    // TFT init
    tft.init();
    tft.setRotation(0);  // Portrait 240x320
    tft.writecommand(0x36);
    tft.writedata(0x08);  // BGR
    tft.fillScreen(TFT_BLACK);
    digitalWrite(PIN_LCD_BL, HIGH);

    // LVGL init
    lv_init();

    static uint8_t draw_buf[SCREEN_W * 40] __attribute__((aligned(4)));
    lv_display_t *disp = lv_tft_espi_create(SCREEN_W, SCREEN_H, draw_buf, sizeof(draw_buf));
    if (!disp) {
        Serial.println("[FATAL] LVGL display create failed!");
        while (1) delay(1000);
    }
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);

    // Keypad input device
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, keypad_read);

    // Create default group for focus navigation
    lv_group_t *group = lv_group_create();
    lv_group_set_default(group);
    lv_indev_set_group(indev, group);

    // Button pins init
    for (int i = 0; i < 7; i++) {
        pinMode(btn_pins[i], INPUT_PULLUP);
    }

    Serial.println("[LVGL] Initialized with 7-key navigation");
}

// =====================================================================
//  Setup & Loop
// =====================================================================
void setup()
{
    lvgl_setup();

    // LittleFS for fonts
    if (!LittleFS.begin(true)) {
        Serial.println("[LittleFS] Mount failed!");
    } else {
        Serial.printf("[LittleFS] OK, total=%dKB used=%dKB\n",
                      LittleFS.totalBytes()/1024, LittleFS.usedBytes()/1024);
    }

    // Init subsystems
    SD_init();
    music_init();
    bt_a2dp_init();
    ui_init();
    task_init();
    WiFi_init();

    Serial.println("[OK] Ready");
}

void loop()
{
    // LVGL tick
    static uint32_t last_tick = 0;
    uint32_t now = millis();
    if (now != last_tick) {
        lv_tick_inc(now - last_tick);
        last_tick = now;
    }

    lv_timer_handler();

    // Long press RST to restart
    static uint32_t rst_press_time = 0;
    if (digitalRead(BTN_RST_PIN) == LOW) {
        if (rst_press_time == 0) rst_press_time = millis();
        else if (millis() - rst_press_time > 1000) ESP.restart();
    } else {
        rst_press_time = 0;
    }

    delay(5);
}
