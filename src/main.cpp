/**
 * @file main.cpp
 * @brief ZLRomi NEO Music Player - 主程序
 * 
 * ESP32-S3 + ST7789(240x320) + PCM5102A I2S
 * LVGL v9 GUI + 拼音输入法 + 7键焦点导航 + 网易云API
 * ESP32-audioI2S 流媒体播放引擎
 */
#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <SPIFFS.h>

#include "types.h"
#include "ui.h"
#include "audio_engine.h"
#include "api.h"
#include "input.h"
#include "settings.h"
#include "usb_host/usb_host_msc.h"

// =====================================================================
//  硬件引脚
// =====================================================================
#define PIN_LCD_BL    42
#define SCREEN_W      240
#define SCREEN_H      320

// =====================================================================
//  全局变量
// =====================================================================
static TFT_eSPI *tft_ptr = nullptr;
static SongInfo g_current_song;
static SongInfo g_search_results[20];
static int g_search_count = 0;
static LyricsData g_lyrics;
static UrlInfo g_play_url;
static bool g_wifi_connected = false;
static int g_playlist_index = -1;

// =====================================================================
//  前向声明
// =====================================================================
static void on_button(ButtonId btn, ButtonEvent evt);
static void on_audio_progress(int pos_ms, int dur_ms);
static void on_audio_state(PlayState state);
static void on_audio_error(const char* msg);
static void play_song_by_index(int index);
static void play_next();
static void play_prev();
static void toggle_play_pause();
static void connect_wifi(const char* ssid, const char* pass);
static void do_search();

// =====================================================================
//  播放控制逻辑
// =====================================================================
static void play_song_by_index(int index) {
    if (index < 0 || index >= g_search_count) return;

    g_playlist_index = index;
    g_current_song = g_search_results[index];
    g_current_song.progress_ms = 0;
    g_current_song.playing = true;

    Serial.printf("[Play] #%d: %s - %s\n", index,
                  g_current_song.name, g_current_song.artist);

    ui_update_song(&g_current_song);
    ui_show_toast("\xe6\xad\xa3\xe5\x9c\xa8\xe5\x8a\xa0\xe8\xbd\xbd...");

    // 获取播放URL (带音质降级)
    const Settings *cfg = settings_get();
    if (api_get_url(g_current_song.id, cfg->quality, &g_play_url)) {
        if (g_play_url.valid) {
            audio_play_url(g_play_url.url);
            ui_navigate_to(STATE_PLAYING);
            ui_show_toast("\xe6\x92\xad\xe6\x94\xbe\xe4\xb8\xad");

            // 加载歌词
            memset(&g_lyrics, 0, sizeof(g_lyrics));
            if (api_get_lyrics(g_current_song.id, &g_lyrics)) {
                Serial.printf("[Lyrics] Loaded %d lines\n", g_lyrics.count);
            }
        } else {
            ui_show_toast("\xe6\x97\xa0\xe6\xb3\x95\xe8\x8e\xb7\xe5\x8f\x96\xe6\x92\xad\xe6\x94\xbe\xe9\x93\xbe\xe6\x8e\xa5");
        }
    } else {
        ui_show_toast("\xe8\x8e\xb7\xe5\x8f\x96\xe6\x92\xad\xe6\x94\xbe\xe9\x93\xbe\xe6\x8e\xa5\xe5\xa4\xb1\xe8\xb4\xa5");
    }
}

static void play_next() {
    if (g_search_count <= 0) return;
    int next = g_playlist_index + 1;
    if (next >= g_search_count) {
        const Settings *cfg = settings_get();
        next = cfg->auto_play ? 0 : -1;
    }
    if (next >= 0) play_song_by_index(next);
    else ui_show_toast("\xe5\xb7\xb2\xe6\x98\xaf\xe6\x9c\x80\xe5\x90\x8e\xe4\xb8\x80\xe9\xa6\x96");
}

static void play_prev() {
    if (g_search_count <= 0) return;
    int prev = g_playlist_index - 1;
    if (prev < 0) {
        const Settings *cfg = settings_get();
        prev = cfg->auto_play ? g_search_count - 1 : -1;
    }
    if (prev >= 0) play_song_by_index(prev);
    else ui_show_toast("\xe5\xb7\xb2\xe6\x98\xaf\xe7\xac\xac\xe4\xb8\x80\xe9\xa6\x96");
}

static void toggle_play_pause() {
    PlayState st = audio_get_state();
    if (st == PLAY_PLAYING) {
        audio_pause();
        g_current_song.playing = false;
    } else if (st == PLAY_PAUSED) {
        audio_resume();
        g_current_song.playing = true;
    } else if (g_playlist_index >= 0 && g_play_url.valid) {
        audio_play_url(g_play_url.url);
        g_current_song.playing = true;
    }
}

// =====================================================================
//  搜索执行
// =====================================================================
static void do_search() {
    const char* kw = ui_get_search_keyword();
    if (!kw || strlen(kw) == 0) {
        ui_show_toast("\xe8\xaf\xb7\xe8\xbe\x93\xe5\x85\xa5\xe6\x90\x9c\xe7\xb4\xa2\xe5\x85\xb3\xe9\x94\xae\xe8\xaf\x8d");
        return;
    }

    ui_show_toast("\xe6\x90\x9c\xe7\xb4\xa2\xe4\xb8\xad...");
    Serial.printf("[Search] Keyword: %s\n", kw);

    g_search_count = api_search(kw, g_search_results, 20);
    if (g_search_count > 0) {
        ui_update_search_results(g_search_results, g_search_count);
        char buf[64];
        snprintf(buf, sizeof(buf), "\xe6\x89\xbe\xe5\x88\xb0 %d \xe9\xa6\x96\xe6\xad\x8c\xe6\x9b\xb2", g_search_count);
        ui_show_toast(buf);
    } else if (g_search_count == 0) {
        ui_show_toast("\xe6\x9c\xaa\xe6\x89\xbe\xe5\x88\xb0\xe7\xbb\x93\xe6\x9e\x9c");
    } else {
        ui_show_toast("\xe6\x90\x9c\xe7\xb4\xa2\xe5\xa4\xb1\xe8\xb4\xa5");
    }
}

// =====================================================================
//  WiFi连接
// =====================================================================
static void connect_wifi(const char* ssid, const char* pass) {
    if (!ssid || strlen(ssid) == 0) {
        ui_show_toast("SSID\xe4\xb8\xba\xe7\xa9\xba");
        return;
    }

    Serial.printf("[WiFi] Connecting to %s...\n", ssid);
    ui_show_toast("\xe8\xbf\x9e\xe6\x8e\xa5WiFi\xe4\xb8\xad...");

    WiFi.begin(ssid, pass);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        attempts++;
    }

    g_wifi_connected = (WiFi.status() == WL_CONNECTED);
    ui_set_wifi_status(g_wifi_connected);
    if (g_wifi_connected) {
        Serial.println("[WiFi] Connected");
        Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
        ui_show_toast("WiFi\xe5\xb7\xb2\xe8\xbf\x9e\xe6\x8e\xa5");
        settings_set_wifi(ssid, pass);
        api_init();
    } else {
        Serial.println("[WiFi] Failed");
        ui_show_toast("WiFi\xe8\xbf\x9e\xe6\x8e\xa5\xe5\xa4\xb1\xe8\xb4\xa5");
    }
}

// =====================================================================
//  按键事件处理 (完整的7键导航)
// =====================================================================
static void on_button(ButtonId btn, ButtonEvent evt) {
    // RST长按重启
    if (evt == EVT_LONG_PRESS && btn == BTN_RST) {
        ESP.restart();
        return;
    }

    if (evt != EVT_PRESS) return;

    AppState state = ui_get_state();

    switch (state) {
        case STATE_MAIN:
            if (btn == BTN_LEFT) {
                play_prev();
            } else if (btn == BTN_RIGHT) {
                play_next();
            } else if (btn == BTN_OK) {
                toggle_play_pause();
            } else if (btn == BTN_SET) {
                ui_navigate_to(STATE_SEARCH);
            } else if (btn == BTN_DOWN) {
                ui_navigate_to(STATE_SETTINGS);
            } else if (btn == BTN_UP) {
                // 如果有正在播放的歌，进入歌词页
                if (g_playlist_index >= 0 && g_lyrics.has_lyrics) {
                    ui_navigate_to(STATE_LYRICS);
                }
            }
            break;

        case STATE_SEARCH:
            if (btn == BTN_SET) {
                ui_navigate_to(STATE_MAIN);
            } else if (btn == BTN_OK) {
                // 如果搜索结果有内容且keyboard已隐藏，选择当前焦点歌曲
                if (g_search_count > 0) {
                    int fi = ui_get_focus_index();
                    if (fi >= 0 && fi < g_search_count) {
                        play_song_by_index(fi);
                    } else {
                        do_search();
                    }
                } else {
                    do_search();
                }
            }
            // 其他按键交给LVGL keyboard/textarea处理
            break;

        case STATE_PLAYING:
            if (btn == BTN_OK) {
                toggle_play_pause();
            } else if (btn == BTN_LEFT) {
                play_prev();
            } else if (btn == BTN_RIGHT) {
                play_next();
            } else if (btn == BTN_UP) {
                ui_navigate_to(STATE_LYRICS);
            } else if (btn == BTN_SET) {
                ui_navigate_to(STATE_MAIN);
            }
            break;

        case STATE_LYRICS:
            if (btn == BTN_SET || btn == BTN_DOWN) {
                ui_navigate_to(STATE_PLAYING);
            }
            break;

        case STATE_SETTINGS:
            if (btn == BTN_UP) {
                ui_move_focus(-1);
            } else if (btn == BTN_DOWN) {
                ui_move_focus(1);
            } else if (btn == BTN_OK) {
                int fi = ui_get_focus_index();
                switch (fi) {
                    case 0: // WiFi设置
                        ui_navigate_to(STATE_WIFI);
                        break;
                    case 1: { // 音量调节
                        Settings *s = settings_get_mutable();
                        // 映射 0-100 -> 0-21
                        int hw_vol = audio_get_volume();
                        hw_vol = (hw_vol + 1) % 22;
                        audio_set_volume(hw_vol);
                        s->volume = (hw_vol * 100) / 21;
                        settings_save();
                        char buf[32];
                        snprintf(buf, sizeof(buf), "\xe9\x9f\xb3\xe9\x87\x8f: %d/21", hw_vol);
                        ui_show_toast(buf);
                        break;
                    }
                    case 2: { // 音质选择
                        Settings *s = settings_get_mutable();
                        s->quality = (AudioQuality)((s->quality + 1) % QUALITY_COUNT);
                        settings_save();
                        const char *qnames[] = {
                            "\xe6\xa0\x87\xe5\x87\x86 128k",
                            "\xe9\xab\x98\xe8\xb4\xa8 320k",
                            "\xe6\x97\xa0\xe6\x8d\x9f FLAC"
                        };
                        char buf[32];
                        snprintf(buf, sizeof(buf), "\xe9\x9f\xb3\xe8\xb4\xa8: %s", qnames[s->quality]);
                        ui_show_toast(buf);
                        break;
                    }
                    case 3: { // 蓝牙开关
                        Settings *s = settings_get_mutable();
                        s->bt_enabled = !s->bt_enabled;
                        settings_save();
                        ui_show_toast(s->bt_enabled ?
                            "\xe8\x93\x9d\xe7\x89\x99: \xe5\xbc\x80" :
                            "\xe8\x93\x9d\xe7\x89\x99: \xe5\x85\xb3");
                        break;
                    }
                    case 4: { // 自动播放
                        Settings *s = settings_get_mutable();
                        s->auto_play = !s->auto_play;
                        settings_save();
                        ui_show_toast(s->auto_play ?
                            "\xe8\x87\xaa\xe5\x8a\xa8\xe6\x92\xad\xe6\x94\xbe: \xe5\xbc\x80" :
                            "\xe8\x87\xaa\xe5\x8a\xa8\xe6\x92\xad\xe6\x94\xbe: \xe5\x85\xb3");
                        break;
                    }
                    case 5: // 关于
                        ui_show_toast("NEO Music v1.0 by ZLRomi");
                        break;
                }
            } else if (btn == BTN_SET) {
                ui_navigate_to(STATE_MAIN);
            }
            break;

        case STATE_WIFI:
            if (btn == BTN_SET) {
                ui_navigate_to(STATE_SETTINGS);
            } else if (btn == BTN_OK) {
                // 连接WiFi
                const char *ssid = ui_get_wifi_ssid();
                const char *pass = ui_get_wifi_pass();
                connect_wifi(ssid, pass);
            }
            break;
    }
}

// =====================================================================
//  音频回调
// =====================================================================
static void on_audio_progress(int pos_ms, int dur_ms) {
    g_current_song.progress_ms = pos_ms;
    g_current_song.duration_ms = dur_ms;
}

static void on_audio_state(PlayState state) {
    Serial.printf("[Audio] State: %d\n", state);
    if (state == PLAY_ERROR) {
        ui_show_toast("\xe6\x92\xad\xe6\x94\xbe\xe5\x87\xba\xe9\x94\x99");
        g_current_song.playing = false;
    } else if (state == PLAY_STOPPED) {
        // 播放结束，自动下一首
        if (g_current_song.playing) {
            g_current_song.playing = false;
            play_next();
        }
    }
}

static void on_audio_error(const char* msg) {
    Serial.printf("[Audio] Error: %s\n", msg);
    ui_show_toast(msg);
}

// =====================================================================
//  SETUP
// =====================================================================
void setup() {
    // UART0 on GPIO43(TX)/GPIO44(RX) for debug (USB OTG used for MSC)
    Serial.begin(115200, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
    delay(500);
    Serial.println("\n=== ZLRomi NEO Music Player ===");
    Serial.println("[1] Starting...");

    // 初始化SPIFFS（备用存储）
    if (!SPIFFS.begin(true)) {
        Serial.println("[SPIFFS] Mount failed!");
    } else {
        Serial.printf("[SPIFFS] OK, total=%dKB used=%dKB\n",
                      SPIFFS.totalBytes()/1024, SPIFFS.usedBytes()/1024);
    }

    // 初始化USB Host MSC（U盘读取字体文件）
    Serial.println("[USB] Initializing USB Host MSC...");
    usb_host_init();
    delay(2000);  // Wait for USB enumeration
    Serial.printf("[USB] mounted=%d\n", usb_host_is_mounted());

    // 初始化设置
    Serial.println("[2] Settings init...");
    settings_init();
    const Settings *cfg = settings_get();
    Serial.printf("[3] Config: vol=%d bt=%d quality=%d\n",
                  cfg->volume, cfg->bt_enabled, cfg->quality);

    // 初始化屏幕背光
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, LOW);

    // 初始化TFT
    static TFT_eSPI tft_instance;
    tft_ptr = &tft_instance;
    tft_ptr->init();
    tft_ptr->setRotation(0);
    tft_ptr->writecommand(0x36);
    tft_ptr->writedata(0x08);  // BGR
    tft_ptr->fillScreen(TFT_BLACK);
    digitalWrite(PIN_LCD_BL, HIGH);

    // 初始化LVGL
    lv_init();
    static uint8_t draw_buf[SCREEN_W * 40] __attribute__((aligned(4)));
    lv_display_t *disp = lv_tft_espi_create(SCREEN_W, SCREEN_H,
                                             draw_buf, sizeof(draw_buf));
    if (!disp) {
        Serial.println("[FATAL] LVGL display create failed!");
        digitalWrite(PIN_LCD_BL, HIGH);
        tft_ptr->fillScreen(TFT_RED);
        while(1) delay(1000);
    }
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);

    // 创建keypad输入设备
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, input_lvgl_read_cb);

    lv_group_t *group = lv_group_get_default();
    if (!group) {
        group = lv_group_create();
        lv_group_set_default(group);
    }
    lv_indev_set_group(indev, group);

    // 深色主题
    lv_theme_t *th = lv_theme_default_init(disp,
        lv_color_hex(0xFF6B6B), lv_color_hex(0x4ECDC4),
        true, &lv_font_montserrat_14);
    lv_disp_set_theme(disp, th);

    // 初始化各模块
    input_init();
    input_set_callback(on_button);

    ui_init();
    ui_create_screens();

    // 初始化音频引擎 (ESP32-audioI2S)
    audio_engine_init();
    // 映射设置中的0-100音量到0-21
    int hw_vol = (cfg->volume * 21) / 100;
    if (hw_vol > 21) hw_vol = 21;
    audio_set_volume(hw_vol);
    audio_set_progress_callback(on_audio_progress);
    audio_set_state_callback(on_audio_state);
    audio_set_error_callback(on_audio_error);

    // 连接WiFi
    if (strlen(cfg->wifi_ssid) > 0) {
        connect_wifi(cfg->wifi_ssid, cfg->wifi_pass);
    } else {
        ui_set_wifi_status(false);
        Serial.println("[WiFi] No saved SSID, please configure via Settings");
    }

    // 启动画面 -> 主界面
    ui_navigate_to(STATE_BOOT);
    delay(1500);
    ui_navigate_to(STATE_MAIN);

    Serial.println("[OK] Ready");
}

// =====================================================================
//  LOOP
// =====================================================================
void loop() {
    // LVGL tick
    static uint32_t last_tick = 0;
    uint32_t now = millis();
    if (now != last_tick) {
        lv_tick_inc(now - last_tick);
        last_tick = now;
    }

    // 更新输入
    input_update();

    // LVGL处理
    lv_timer_handler();

    // 音频引擎核心循环 (ESP32-audioI2S解码)
    audio_task();

    // 更新进度显示和歌词同步
    if (g_current_song.playing || audio_is_running()) {
        int pos = audio_get_position_ms();
        int dur = audio_get_duration_ms();
        ui_update_progress(pos, dur);

        // 歌词同步
        if (ui_get_state() == STATE_LYRICS && g_lyrics.has_lyrics) {
            int line = lyrics_get_current_line(&g_lyrics, pos);
            if (line >= 0) {
                ui_update_lyrics(&g_lyrics, line);
            }
        }
    }

    // Toast自动隐藏
    ui_check_toast_timeout();

    delay(5);
}