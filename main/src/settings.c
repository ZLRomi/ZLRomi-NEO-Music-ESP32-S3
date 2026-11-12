#include "settings.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";

app_settings_t g_cfg;

static void defaults(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.volume     = 70;
    g_cfg.brightness = 80;
    g_cfg.rotation   = 0;
    g_cfg.quality    = 0;
    g_cfg.play_mode  = 0;
    g_cfg.boot_anim  = 1;
}

void settings_init(void)
{
    defaults();

    nvs_handle_t h;
    if (nvs_open("neo", NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "首次启动，使用默认设置");
        return;
    }

    app_settings_t tmp;
    size_t len = sizeof(tmp);
    if (nvs_get_blob(h, "cfg", &tmp, &len) == ESP_OK && len == sizeof(tmp)) {
        /* 逐字段校验，避免旧版本结构体残留值越界 */
        g_cfg.volume     = tmp.volume > 100 ? 70 : tmp.volume;
        g_cfg.brightness = tmp.brightness > 100 ? 80 : tmp.brightness;
        g_cfg.rotation   = tmp.rotation > 3 ? 0 : tmp.rotation;
        g_cfg.quality    = tmp.quality > 2 ? 0 : tmp.quality;
        g_cfg.play_mode  = tmp.play_mode > 2 ? 0 : tmp.play_mode;
        g_cfg.boot_anim  = tmp.boot_anim > 1 ? 1 : tmp.boot_anim;
        memcpy(g_cfg.wifi_ssid, tmp.wifi_ssid, sizeof(g_cfg.wifi_ssid));
        memcpy(g_cfg.wifi_pass, tmp.wifi_pass, sizeof(g_cfg.wifi_pass));
        g_cfg.wifi_ssid[sizeof(g_cfg.wifi_ssid) - 1] = '\0';
        g_cfg.wifi_pass[sizeof(g_cfg.wifi_pass) - 1] = '\0';
        ESP_LOGI(TAG, "已载入设置：音量 %d，WiFi %s",
                 g_cfg.volume, g_cfg.wifi_ssid[0] ? g_cfg.wifi_ssid : "未配置");
    }
    nvs_close(h);
}

void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open("neo", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "NVS 打开失败，设置未保存");
        return;
    }
    esp_err_t err = nvs_set_blob(h, "cfg", &g_cfg, sizeof(g_cfg));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "写入设置失败: %s", esp_err_to_name(err));
    }
    nvs_close(h);
}
