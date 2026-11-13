/*
 * main.c —— NEO Music 启动流程
 *
 * 顺序：NVS -> 设置 -> 屏幕 -> 按键 -> WiFi -> 音乐状态 -> 播放器 -> UI。
 * 屏幕整帧缓冲占 150KB 内部 RAM，必须在 WiFi 之前分配。
 * 推荐歌单/每日新歌放后台任务拉取，开机先亮屏不白等网络。
 */
#include <stdbool.h>
#include <stdio.h>

#include "board.h"
#include "cover.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gfx.h"
#include "input.h"
#include "music_state.h"
#include "nvs_flash.h"
#include "player.h"
#include "settings.h"
#include "ui.h"
#include "wifi_mgr.h"

static const char *TAG = "app";

static void on_track_end(void)
{
    /* 单曲循环时重播当前首，其余按播放模式接下一首 */
    if (g_music.mode == MODE_LOOP_ONE) {
        music_play_current();
    } else {
        music_next();
    }
}

static volatile bool s_loading;

/**
 * 拿到 IP 后拉首页数据。
 *
 * 不再单独起任务：以前这里起 16KB 栈的 load_home_task 专门拉歌单，
 * 白占 16KB 内部 RAM —— 而内部 RAM 恰恰是 TLS 握手的命门。
 * 现在直接投递给 net_task 串行处理，数据到了它自己调 ui_redraw()。
 */
static void on_wifi_connected(void)
{
    if (s_loading) {
        return;
    }
    s_loading = true;
    music_load_home_async();
    s_loading = false;
}

void app_main(void)
{
    ESP_LOGI(TAG, "NEO Music %s / HW-678 ESP32-S3", FW_VERSION);

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    settings_init();

    gfx_init();
    gfx_brightness(g_cfg.brightness);
    gfx_set_rotation(g_cfg.rotation);
    input_init();

    wifi_mgr_init();
/* 音频任务最先建：它要 24KB 连续内部 RAM，赶在歌词/封面任务把内部堆碎片化之前，
   才有最大的连续块可用。 */
    player_init();
    player_set_end_callback(on_track_end);
    music_state_init();
    cover_init();
    ui_init();

    ESP_LOGI(TAG, "按键：方向键移动焦点，OK 确认，SET 返回，RST 短按息屏/长按 3 秒重启页");
    ESP_LOGI(TAG, "配网热点：%s", wifi_mgr_ap_ssid());

    /* 内存余量：内部 RAM 最紧张（WiFi+TLS+任务栈都从这里出），
       PSRAM 反而是富余的一方。启动后打一次，方便判断还能不能再加栈。 */
    ESP_LOGI(TAG, "内存：内部可用 %u 字节（最大块 %u），PSRAM 可用 %u 字节",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    wifi_mgr_set_connected_callback(on_wifi_connected);

    while (true) {
        ui_tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
