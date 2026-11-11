/**
 * @file main.cpp
 * @brief ZLRomi NEO Music Player - 主程序
 * 
 * ESP32-S3 + ST7789 + PCM5102A I2S + Bluetooth A2DP
 * LVGL GUI + 拼音输入法 + 7键焦点导航 + 网易云API
 */
#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>

#include "types.h"
#include "ui.h"
#include "audio.h"
#include "api.h"
#include "input.h"
#include "settings.h"

// =====================================================================
//  硬件引脚
// =====================================================================
#define PIN_LCD_BL    42
#define SCREEN_W      240
#define SCREEN_H      320

// =====================================================================
//  全局变量
// =====================================================================
static TFT_eSPI tft;
static SongInfo g_current_song;
static SongInfo g_search_results[20];
static int g_search_count = 0;
static LyricsData g_lyrics;
static UrlInfo g_play_url;
static bool g_wifi_connected = false;
static int g_playlist_index = -1;

// Toast自动隐藏计时
static uint32_t s_toast_start = 0;
#define TOAST_DURATION_MS 2500

// =====================================================================
//  回调函数
// =====================================================================
static void on_button(ButtonId btn, ButtonEvent evt);
static void on_audio_progress(int pos_ms, int dur_ms);
static void on_audio_state(PlayState state);
static void on_audio_error(const char* msg);

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

    // 更新UI
    ui_update_song(&g_current_song);

    // 获取播放URL
    const Settings *cfg = settings_get();
    if (api_get_url(g_current_song.id, cfg->quality, &g_play_url)) {
        if (g_play_url.valid) {
            audio_play_url(g_play_url.url);
            ui_navigate_to(STATE_PLAYING);

            // 异步加载歌词
            if (api_get_lyrics(g_current_song.id, &g_lyrics)) {
                Serial.printf("[Lyrics] Loaded %d lines\n", g_lyrics.count);
            }
        } else {
            ui_show_toast("无法获取播放链接");
        }
    } else {
        ui_show_toast("获取播放链接失败");
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
    else ui_show_toast("已是最后一首");
}

static void play_prev() {
    if (g_search_count <= 0) return;
    int prev = g_playlist_index - 1;
    if (prev < 0) {
        const Settings *cfg = settings_get();
        prev = cfg->auto_play ? g_search_count - 1 : -1;
    }
    if (prev >= 0) play_song_by_index(prev);
    else ui_show_toast("已是第一首");
}

static void toggle_play_pause() {
    PlayState st = audio_get_state();
    if (st == PLAY_PLAYING) {
        audio_pause();
        g_current_song.playing = false;
    } else if (st == PLAY_PAUSED) {
        audio_resume();
        g_current_song.playing = true;
    } else if (g_playlist_index >= 0) {
        // 重新播放当前歌曲
        if (g_play_url.valid) {
            audio_play_url(g_play_url.url);
            g_current_song.playing = true;
        }
    }
}

// =====================================================================
//  WiFi连接
// =====================================================================
static void connect_wifi(const char* ssid, const char* pass) {
    Serial.printf("[WiFi] Connecting to %s...\n", ssid);
    WiFi.begin(ssid, pass);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        attempts++;
    }

    g_wifi_connected = (WiFi.status() == WL_CONNECTED);
    if (g_wifi_connected) {
        Serial.println("[WiFi] Connected");
        ui_show_toast("WiFi 已连接");
        settings_set_wifi(ssid, pass);
    } else {
        Serial.println("[WiFi] Failed");
        ui_show_toast("WiFi 连接失败");
    }
}

// =====================================================================
//  按键事件处理 (完整的7键导航)
// =====================================================================
static void on_button(ButtonId btn, ButtonEvent evt) {
    if (evt == EVT_LONG_PRESS && btn == BTN_RST) {
        ESP.restart();
        return;
    }

    if (evt != EVT_PRESS) return;

    AppState state = ui_get_state();

    switch (state) {
        case STATE_MAIN:
            if (btn == BTN_UP) {
                // 主界面没有列表，UP可以进入搜索
                ui_navigate_to(STATE_SEARCH);
            } else if (btn == BTN_DOWN) {
                ui_navigate_to(STATE_SETTINGS);
            } else if (btn == BTN_LEFT) {
                play_prev();
            } else if (btn == BTN_RIGHT) {
                play_next();
            } else if (btn == BTN_OK) {
                toggle_play_pause();
            } else if (btn == BTN_SET) {
                ui_navigate_to(STATE_SEARCH);
            }
            break;

        case STATE_SEARCH:
            if (btn == BTN_SET) {
                ui_navigate_to(STATE_MAIN);
            } else if (btn == BTN_OK) {
                // 如果textarea有焦点，键盘会处理
                // 如果搜索结果列表有焦点，播放选中项
                int fi = ui_get_focus_index();
                if (fi >= 0 && fi < g_search_count) {
                    play_song_by_index(fi);
                }
            } else if (btn == BTN_UP) {
                ui_move_focus(-1);
            } else if (btn == BTN_DOWN) {
                ui_move_focus(1);
            }
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
            } else if (btn == BTN_UP) {
                // 手动向上滚动歌词(可选)
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
                    case 0: // WiFi配置
                        ui_navigate_to(STATE_WIFI);
                        break;
                    case 1: { // 音量调节
                        Settings *s = settings_get_mutable();
                        s->volume = (s->volume + 10) % 110;
                        if (s->volume > 100) s->volume = 0;
                        audio_set_volume(s->volume);
                        settings_save();
                        char buf[20];
                        snprintf(buf, sizeof(buf), "音量: %d%%", s->volume);
                        ui_show_toast(buf);
                        break;
                    }
                    case 2: { // 音质选择
                        Settings *s = settings_get_mutable();
                        s->quality = (AudioQuality)((s->quality + 1) % QUALITY_COUNT);
                        settings_save();
                        const char *qnames[] = {"标准 128k", "高质 320k", "无损 FLAC"};
                        char buf[30];
                        snprintf(buf, sizeof(buf), "音质: %s", qnames[s->quality]);
                        ui_show_toast(buf);
                        break;
                    }
                    case 3: { // 蓝牙开关
                        Settings *s = settings_get_mutable();
                        s->bt_enabled = !s->bt_enabled;
                        settings_save();
                        ui_show_toast(s->bt_enabled ? "蓝牙: 开" : "蓝牙: 关");
                        break;
                    }
                    case 4: { // 自动播放
                        Settings *s = settings_get_mutable();
                        s->auto_play = !s->auto_play;
                        settings_save();
                        ui_show_toast(s->auto_play ? "自动播放: 开" : "自动播放: 关");
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
                const char *ssid = lv_textarea_get_text(ui_get_search_textarea());
                // WiFi界面有自己的textarea，这里简化处理
                // 实际应从s_ta_ssid和s_ta_pass获取
                ui_show_toast("请使用键盘输入后按OK连接");
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
        ui_show_toast("播放出错");
        g_current_song.playing = false;
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
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== ZLRomi NEO Music Player ===");

    // 初始化设置
    settings_init();
    const Settings *cfg = settings_get();

    // 初始化屏幕背光
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, LOW);

    // 初始化TFT
    tft.init();
    tft.setRotation(0);
    tft.writecommand(0x36);
    tft.writedata(0x08);  // BGR
    tft.fillScreen(TFT_BLACK);
    analogWrite(PIN_LCD_BL, cfg->screen_brightness);

    // 初始化LVGL
    lv_init();
    static uint8_t draw_buf[SCREEN_W * 40] __attribute__((aligned(4))); // 减小buffer
    lv_display_t *disp = lv_tft_espi_create(SCREEN_W, SCREEN_H,
                                             draw_buf, sizeof(draw_buf));
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);

    // 创建keypad输入设备
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, input_lvgl_read_cb);

    // 主题
    lv_theme_t *th = lv_theme_default_init(disp,
        lv_color_hex(0xFF6B6B), lv_color_hex(0x4ECDC4),
        true, &lv_font_montserrat_14);
    lv_disp_set_theme(disp, th);

    // 初始化各模块
    input_init();
    input_set_callback(on_button);

    ui_init();
    ui_create_screens();

    audio_init();
    audio_set_volume(cfg->volume);
    audio_set_quality(cfg->quality);
    audio_set_progress_callback(on_audio_progress);
    audio_set_state_callback(on_audio_state);
    audio_set_error_callback(on_audio_error);

    api_init();

    // 连接WiFi
    if (strlen(cfg->wifi_ssid) > 0) {
        connect_wifi(cfg->wifi_ssid, cfg->wifi_pass);
    }

    // 启动画面
    ui_navigate_to(STATE_BOOT);
    delay(2000);
    ui_navigate_to(STATE_MAIN);

    Serial.println("[OK] Ready");
}

// =====================================================================
//  LOOP
// =====================================================================
void loop() {
    // 更新输入
    input_update();

    // LVGL处理
    lv_timer_handler();

    // 音频后台任务
    audio_task();

    // 更新进度显示
    if (g_current_song.playing) {
        ui_update_progress(audio_get_position_ms(), audio_get_duration_ms());

        // 更新歌词同步
        if (ui_get_state() == STATE_LYRICS && g_lyrics.has_lyrics) {
            int line = lyrics_get_current_line(&g_lyrics,
                                                audio_get_position_ms());
            if (line >= 0) {
                ui_update_lyrics(&g_lyrics, line);
            }
        }
    }

    // Toast自动隐藏
    if (s_toast_start > 0 && millis() - s_toast_start > TOAST_DURATION_MS) {
        // Toast由LVGL layer管理，这里不做额外处理
        s_toast_start = 0;
    }

    delay(5);
}
