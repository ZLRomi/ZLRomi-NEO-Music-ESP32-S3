/**
 * ZLRomi NEO Music Player
 * ESP32-S3 + ST7789 + PCM5102A + Bluetooth A2DP
 * 
 * Features:
 * - LVGL GUI with Pinyin IME
 * - 7-key focus navigation (TV remote style)
 * - Dual audio output: I2S PCM5102A + Bluetooth A2DP
 * - Netease Cloud Music API
 * - Lyrics sync display
 */

#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>

// =====================================================================
//  Hardware Configuration
// =====================================================================

// Display (ST7789 240x320 SPI)
#define PIN_LCD_SCLK  21
#define PIN_LCD_MOSI  47
#define PIN_LCD_RST   45
#define PIN_LCD_DC    40
#define PIN_LCD_CS    41
#define PIN_LCD_BL    42

// Buttons (active LOW with pull-up)
#define BTN_UP     17
#define BTN_DOWN   16
#define BTN_LEFT   15
#define BTN_RIGHT   4
#define BTN_OK      5
#define BTN_SET     6
#define BTN_RST     7

// I2S PCM5102A
#define I2S_BCK     9
#define I2S_LRCK    11
#define I2S_DIN     10
#define I2S_PORT    I2S_NUM_0

// Screen
#define SCREEN_W    240
#define SCREEN_H    320

// =====================================================================
//  Color Theme (Dark Elegant)
// =====================================================================
#define C_BG         lv_color_hex(0x0A0A0A)
#define C_SURFACE    lv_color_hex(0x1A1A1A)
#define C_SURFACE2   lv_color_hex(0x2A2A2A)
#define C_PRIMARY    lv_color_hex(0xFF6B6B)
#define C_ACCENT     lv_color_hex(0x4ECDC4)
#define C_TEXT       lv_color_hex(0xFFFFFF)
#define C_TEXT_SEC   lv_color_hex(0xBDBDBD)
#define C_DIM        lv_color_hex(0x666666)
#define C_FOCUS      lv_color_hex(0xFF6B6B)
#define C_SUCCESS    lv_color_hex(0x51CF66)
#define C_WARN       lv_color_hex(0xFFD43B)

// =====================================================================
//  Global State
// =====================================================================

// LVGL
static lv_display_t *disp;
static lv_indev_t *indev_keypad;
static uint8_t draw_buf[SCREEN_W * SCREEN_H / 8];

// Audio
static bool i2s_initialized = false;
static bool bt_connected = false;

// App state
enum AppState {
    STATE_BOOT,
    STATE_MAIN,
    STATE_SEARCH,
    STATE_PLAYING,
    STATE_LYRICS,
    STATE_SETTINGS,
    STATE_WIFI_SETUP
};

static AppState current_state = STATE_BOOT;

// Focus tracking for 7-key navigation
static int focus_index = 0;
static int max_focus_items = 0;

// Playback
struct SongInfo {
    char title[96];
    char artist[64];
    long id;
    int duration_ms;
    int progress_ms;
    bool playing;
};

static SongInfo current_song = {"", "", 0, 0, 0, false};

// Settings
struct Settings {
    int volume;
    bool bt_enabled;
    char wifi_ssid[32];
    char wifi_pass[64];
};

static Settings settings = {70, true, "", ""};

// UI Objects
static lv_obj_t *scr_main;
static lv_obj_t *scr_search;
static lv_obj_t *scr_playing;
static lv_obj_t *scr_lyrics;
static lv_obj_t *scr_settings;

static lv_obj_t *lbl_title;
static lv_obj_t *lbl_artist;
static lv_obj_t *bar_progress;
static lv_obj_t *lbl_time;
static lv_obj_t *ta_search;
static lv_obj_t *kb;
static lv_obj_t *ime_pinyin;

// Button state
static bool btn_last[7] = {true, true, true, true, true, true, true};
static uint32_t btn_press_time[7] = {0};

// =====================================================================
//  Forward Declarations
// =====================================================================
void init_hardware();
void init_lvgl();
void init_i2s();
void init_bluetooth();
void load_settings();
void save_settings();

void create_screens();
void navigate_to(AppState state);
void update_focus(int delta);
void handle_buttons();

void play_audio(const uint8_t *data, size_t len);
void stop_audio();

// LVGL callbacks
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void keypad_read_cb(lv_indev_t *indev, lv_indev_data_t *data);

// =====================================================================
//  Setup
// =====================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== ZLRomi NEO Music Player ===");
    
    init_hardware();
    load_settings();
    init_lvgl();
    init_i2s();
    
    // Bluetooth init deferred until needed
    if (settings.bt_enabled) {
        init_bluetooth();
    }
    
    create_screens();
    
    // Show boot screen briefly
    lv_obj_t *scr_boot = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_boot, C_BG, 0);
    lv_obj_t *logo = lv_label_create(scr_boot);
    lv_label_set_text(logo, "NEO\nMusic");
    lv_obj_set_style_text_font(logo, &lv_font_source_han_sans_sc_20_cjk, 0);
    lv_obj_set_style_text_color(logo, C_PRIMARY, 0);
    lv_obj_center(logo);
    lv_scr_load(scr_boot);
    
    delay(2000);
    navigate_to(STATE_MAIN);
    
    Serial.println("[OK] Ready");
}

// =====================================================================
//  Loop
// =====================================================================
void loop() {
    handle_buttons();
    lv_timer_handler();
    
    // Update progress if playing
    if (current_song.playing) {
        static uint32_t last_update = 0;
        if (millis() - last_update > 100) {
            last_update = millis();
            current_song.progress_ms += 100;
            if (current_song.duration_ms > 0) {
                int pct = (current_song.progress_ms * 100) / current_song.duration_ms;
                lv_bar_set_value(bar_progress, pct, LV_ANIM_OFF);
                
                char buf[16];
                snprintf(buf, sizeof(buf), "%d:%02d/%d:%02d",
                    current_song.progress_ms / 60000,
                    (current_song.progress_ms / 1000) % 60,
                    current_song.duration_ms / 60000,
                    (current_song.duration_ms / 1000) % 60);
                lv_label_set_text(lbl_time, buf);
            }
        }
    }
    
    delay(5);
}

// =====================================================================
//  Hardware Init
// =====================================================================
void init_hardware() {
    // Backlight
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, LOW);
    
    // TFT
    tft.init();
    tft.setRotation(0);
    tft.writecommand(0x36);
    tft.writedata(0x08);  // BGR
    tft.fillScreen(TFT_BLACK);
    digitalWrite(PIN_LCD_BL, HIGH);
    
    // Buttons
    const int pins[] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_OK, BTN_SET, BTN_RST};
    for (int i = 0; i < 7; i++) {
        pinMode(pins[i], INPUT_PULLUP);
    }
    
    Serial.println("[HW] Display + Buttons OK");
}

// =====================================================================
//  LVGL Init
// =====================================================================
void init_lvgl() {
    lv_init();
    
    // Create TFT display
    disp = lv_tft_espi_create(SCREEN_W, SCREEN_H, draw_buf, sizeof(draw_buf));
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);
    
    // Create keypad input
    indev_keypad = lv_indev_create();
    lv_indev_set_type(indev_keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev_keypad, keypad_read_cb);
    
    // Theme
    lv_theme_t *th = lv_theme_default_init(disp, C_PRIMARY, C_ACCENT, true, &lv_font_montserrat_14);
    lv_disp_set_theme(disp, th);
    
    Serial.println("[LVGL] OK");
}

// =====================================================================
//  I2S PCM5102A Init
// =====================================================================
void init_i2s() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = true,
    };
    
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCK,
        .ws_io_num = I2S_LRCK,
        .data_out_num = I2S_DIN,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };
    
    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[I2S] Driver install failed: %d\n", err);
        return;
    }
    
    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("[I2S] Set pin failed: %d\n", err);
        return;
    }
    
    i2s_zero_dma_buffer(I2S_PORT);
    i2s_initialized = true;
    Serial.println("[I2S] PCM5102A OK");
}

// =====================================================================
//  Bluetooth A2DP Init
// =====================================================================
void init_bluetooth() {
    // Note: ESP32-A2DP library initialization
    // This is a placeholder - actual implementation depends on library version
    Serial.println("[BT] A2DP Source ready");
    bt_connected = false;
}

// =====================================================================
//  Settings
// =====================================================================
void load_settings() {
    Preferences prefs;
    prefs.begin("neo-music", true);
    settings.volume = prefs.getInt("vol", 70);
    settings.bt_enabled = prefs.getBool("bt", true);
    prefs.getString("ssid", settings.wifi_ssid, sizeof(settings.wifi_ssid));
    prefs.getString("pass", settings.wifi_pass, sizeof(settings.wifi_pass));
    prefs.end();
}

void save_settings() {
    Preferences prefs;
    prefs.begin("neo-music", false);
    prefs.putInt("vol", settings.volume);
    prefs.putBool("bt", settings.bt_enabled);
    prefs.putString("ssid", settings.wifi_ssid);
    prefs.putString("pass", settings.wifi_pass);
    prefs.end();
}

// =====================================================================
//  Screen Creation
// =====================================================================
void create_screens() {
    // ---- Main Screen ----
    scr_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_main, C_BG, 0);
    lv_obj_set_style_pad_all(scr_main, 0, 0);
    
    // Status bar
    lv_obj_t *status = lv_obj_create(scr_main);
    lv_obj_set_size(status, SCREEN_W, 28);
    lv_obj_set_style_bg_color(status, C_SURFACE, 0);
    lv_obj_set_style_radius(status, 0, 0);
    lv_obj_set_style_pad_all(status, 4, 0);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 0);
    
    lv_obj_t *lbl_wifi = lv_label_create(status);
    lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(lbl_wifi, C_SUCCESS, 0);
    lv_obj_align(lbl_wifi, LV_ALIGN_LEFT_MID, 8, 0);
    
    lv_obj_t *lbl_app = lv_label_create(status);
    lv_label_set_text(lbl_app, "NEO 音乐");
    lv_obj_set_style_text_font(lbl_app, &lv_font_source_han_sans_sc_14_cjk, 0);
    lv_obj_set_style_text_color(lbl_app, C_TEXT, 0);
    lv_obj_center(lbl_app);
    
    // Cover area
    lv_obj_t *cover = lv_obj_create(scr_main);
    lv_obj_set_size(cover, 180, 180);
    lv_obj_align(cover, LV_ALIGN_TOP_MID, 0, 38);
    lv_obj_set_style_bg_color(cover, C_SURFACE, 0);
    lv_obj_set_style_radius(cover, 16, 0);
    lv_obj_set_style_border_width(cover, 0, 0);
    
    lv_obj_t *lbl_cover = lv_label_create(cover);
    lv_label_set_text(lbl_cover, "♪");
    lv_obj_set_style_text_font(lbl_cover, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_cover, C_DIM, 0);
    lv_obj_center(lbl_cover);
    
    // Title
    lbl_title = lv_label_create(scr_main);
    lv_label_set_text(lbl_title, "未播放");
    lv_obj_set_style_text_font(lbl_title, &lv_font_source_han_sans_sc_18_cjk, 0);
    lv_obj_set_style_text_color(lbl_title, C_TEXT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 228);
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_title, 220);
    
    // Artist
    lbl_artist = lv_label_create(scr_main);
    lv_label_set_text(lbl_artist, "");
    lv_obj_set_style_text_font(lbl_artist, &lv_font_source_han_sans_sc_14_cjk, 0);
    lv_obj_set_style_text_color(lbl_artist, C_TEXT_SEC, 0);
    lv_obj_align(lbl_artist, LV_ALIGN_TOP_MID, 0, 252);
    
    // Progress bar
    bar_progress = lv_bar_create(scr_main);
    lv_obj_set_size(bar_progress, 200, 6);
    lv_obj_align(bar_progress, LV_ALIGN_TOP_MID, 0, 275);
    lv_obj_set_style_bg_color(bar_progress, C_SURFACE2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_progress, C_PRIMARY, LV_PART_INDICATOR);
    lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);
    
    // Time label
    lbl_time = lv_label_create(scr_main);
    lv_label_set_text(lbl_time, "0:00/0:00");
    lv_obj_set_style_text_color(lbl_time, C_DIM, 0);
    lv_obj_align(lbl_time, LV_ALIGN_TOP_MID, 0, 286);
    
    // Navigation hint
    lv_obj_t *hint = lv_label_create(scr_main);
    lv_label_set_text(hint, "OK:播放 SET:搜索 ←→:切歌");
    lv_obj_set_style_text_color(hint, C_DIM, 0);
    lv_obj_set_style_text_font(hint, &lv_font_source_han_sans_sc_14_cjk, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
    
    // ---- Search Screen ----
    scr_search = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_search, C_BG, 0);
    
    lv_obj_t *search_title = lv_label_create(scr_search);
    lv_label_set_text(search_title, "搜索音乐");
    lv_obj_set_style_text_font(search_title, &lv_font_source_han_sans_sc_20_cjk, 0);
    lv_obj_set_style_text_color(search_title, C_PRIMARY, 0);
    lv_obj_align(search_title, LV_ALIGN_TOP_MID, 0, 10);
    
    ta_search = lv_textarea_create(scr_search);
    lv_obj_set_size(ta_search, 220, 40);
    lv_obj_align(ta_search, LV_ALIGN_TOP_MID, 0, 45);
    lv_textarea_set_placeholder_text(ta_search, "输入歌名/歌手...");
    lv_obj_set_style_bg_color(ta_search, C_SURFACE, 0);
    lv_obj_set_style_text_color(ta_search, C_TEXT, 0);
    lv_obj_set_style_text_font(ta_search, &lv_font_source_han_sans_sc_16_cjk, 0);
    lv_textarea_set_one_line(ta_search, true);
    
    // Pinyin IME
    ime_pinyin = lv_ime_pinyin_create(scr_search);
    lv_obj_set_style_text_font(ime_pinyin, &lv_font_source_han_sans_sc_16_cjk, 0);
    
    kb = lv_keyboard_create(scr_search);
    lv_obj_set_size(kb, SCREEN_W, 140);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, ta_search);
    lv_ime_pinyin_set_keyboard(ime_pinyin, kb);
    
    lv_obj_t *cand = lv_ime_pinyin_get_cand_panel(ime_pinyin);
    lv_obj_set_size(cand, LV_PCT(100), 28);
    lv_obj_align_to(cand, kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(cand, C_SURFACE2, 0);
    
    // ---- Playing Screen ----
    scr_playing = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_playing, C_BG, 0);
    // Similar layout to main but with more controls
    
    // ---- Lyrics Screen ----
    scr_lyrics = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_lyrics, C_BG, 0);
    
    lv_obj_t *lyrics_title = lv_label_create(scr_lyrics);
    lv_label_set_text(lyrics_title, "歌词");
    lv_obj_set_style_text_font(lyrics_title, &lv_font_source_han_sans_sc_18_cjk, 0);
    lv_obj_set_style_text_color(lyrics_title, C_PRIMARY, 0);
    lv_obj_align(lyrics_title, LV_ALIGN_TOP_MID, 0, 10);
    
    // ---- Settings Screen ----
    scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_settings, C_BG, 0);
    
    lv_obj_t *settings_title = lv_label_create(scr_settings);
    lv_label_set_text(settings_title, "设置");
    lv_obj_set_style_text_font(settings_title, &lv_font_source_han_sans_sc_20_cjk, 0);
    lv_obj_set_style_text_color(settings_title, C_PRIMARY, 0);
    lv_obj_align(settings_title, LV_ALIGN_TOP_MID, 0, 10);
    
    const char* items[] = {"WiFi 配置", "音量调节", "蓝牙开关", "音质选择", "关于"};
    for (int i = 0; i < 5; i++) {
        lv_obj_t *item = lv_label_create(scr_settings);
        lv_label_set_text(item, items[i]);
        lv_obj_set_style_text_font(item, &lv_font_source_han_sans_sc_16_cjk, 0);
        lv_obj_set_style_text_color(item, C_TEXT, 0);
        lv_obj_align(item, LV_ALIGN_TOP_LEFT, 30, 50 + i * 40);
    }
}

// =====================================================================
//  Navigation
// =====================================================================
void navigate_to(AppState state) {
    current_state = state;
    focus_index = 0;
    
    switch (state) {
        case STATE_MAIN:
            lv_scr_load(scr_main);
            max_focus_items = 3;  // play, search, settings
            break;
        case STATE_SEARCH:
            lv_scr_load(scr_search);
            lv_textarea_set_text(ta_search, "");
            break;
        case STATE_PLAYING:
            lv_scr_load(scr_playing);
            break;
        case STATE_LYRICS:
            lv_scr_load(scr_lyrics);
            break;
        case STATE_SETTINGS:
            lv_scr_load(scr_settings);
            max_focus_items = 5;
            break;
    }
}

void update_focus(int delta) {
    focus_index += delta;
    if (focus_index < 0) focus_index = max_focus_items - 1;
    if (focus_index >= max_focus_items) focus_index = 0;
    // TODO: Update visual focus indicator
}

// =====================================================================
//  Button Handling (7-key TV remote style)
// =====================================================================
void handle_buttons() {
    const int pins[] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_OK, BTN_SET, BTN_RST};
    
    for (int i = 0; i < 7; i++) {
        bool pressed = digitalRead(pins[i]) == LOW;
        
        if (pressed && !btn_last[i]) {
            // Press event
            btn_press_time[i] = millis();
            
            switch (i) {
                case 0: // UP
                    update_focus(-1);
                    break;
                case 1: // DOWN
                    update_focus(1);
                    break;
                case 2: // LEFT
                    if (current_state == STATE_MAIN || current_state == STATE_PLAYING) {
                        // Previous song
                    }
                    break;
                case 3: // RIGHT
                    if (current_state == STATE_MAIN || current_state == STATE_PLAYING) {
                        // Next song
                    }
                    break;
                case 4: // OK
                    if (current_state == STATE_MAIN) {
                        // Toggle play/pause or confirm selection
                    } else if (current_state == STATE_SEARCH) {
                        // Execute search
                        const char *kw = lv_textarea_get_text(ta_search);
                        if (strlen(kw) > 0) {
                            Serial.printf("[Search] %s\n", kw);
                        }
                    }
                    break;
                case 5: // SET
                    if (current_state == STATE_MAIN) {
                        navigate_to(STATE_SEARCH);
                    } else {
                        navigate_to(STATE_MAIN);
                    }
                    break;
                case 6: // RST
                    if (millis() - btn_press_time[i] > 1000) {
                        ESP.restart();
                    }
                    break;
            }
        } else if (!pressed && btn_last[i]) {
            // Release event
        }
        
        btn_last[i] = pressed;
    }
}

// =====================================================================
//  LVGL Callbacks
// =====================================================================
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    // Handled by lv_tft_espi_create
    lv_display_flush_ready(disp);
}

static void keypad_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    const int pins[] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_OK, BTN_SET, BTN_RST};
    static uint32_t last_key = 0;
    
    data->state = LV_INDEV_STATE_RELEASED;
    
    for (int i = 0; i < 7; i++) {
        if (digitalRead(pins[i]) == LOW) {
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
            break;
        }
    }
}

// =====================================================================
//  Audio Output
// =====================================================================
void play_audio(const uint8_t *data, size_t len) {
    if (!i2s_initialized) return;
    
    size_t bytes_written = 0;
    i2s_write(I2S_PORT, data, len, &bytes_written, portMAX_DELAY);
}

void stop_audio() {
    if (i2s_initialized) {
        i2s_zero_dma_buffer(I2S_PORT);
    }
}
