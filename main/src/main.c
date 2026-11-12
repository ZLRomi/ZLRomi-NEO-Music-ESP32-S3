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

static void load_home_task(void *arg)
{
    (void)arg;
    if (music_load_home()) {
        ESP_LOGI(TAG, "首页数据就绪：%d 个歌单，%d 首每日新歌",
                 g_music.playlists.count, g_music.songs.count);
    } else {
        ESP_LOGW(TAG, "在线数据暂不可用，界面会显示空列表");
    }
    ui_redraw();
    vTaskDelete(NULL);
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
    music_state_init();
    player_init();
    player_set_end_callback(on_track_end);
    ui_init();

    ESP_LOGI(TAG, "按键：方向键移动焦点，OK 确认，SET 返回，RST 短按息屏/长按 3 秒重启页");
    ESP_LOGI(TAG, "配网热点：%s", wifi_mgr_ap_ssid());

    xTaskCreateWithCaps(load_home_task, "load_home", 16 * 1024, NULL, 4, NULL,
                        MALLOC_CAP_SPIRAM);

    while (true) {
        ui_tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
