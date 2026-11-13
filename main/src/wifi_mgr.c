#include "wifi_mgr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "lwip/ip4_addr.h"
#include "settings.h"

static const char *TAG = "wifi";

/* 网络初始化失败只记日志并返回，不 abort，避免整机无限重启 */
#define TRY(expr) do {                                                  \
    esp_err_t e_ = (expr);                                              \
    if (e_ != ESP_OK) {                                                 \
        ESP_LOGE(TAG, "%s 失败: %s", #expr, esp_err_to_name(e_));      \
        return;                                                         \
    }                                                                   \
} while (0)

static wifi_state_t s_state = WIFI_OFF;
static void        (*s_connected_cb)(void);
static int          s_rssi = 0;
static int          s_retry = 0;
static char         s_ip[16] = "0.0.0.0";
static char         s_ap_ssid[32];
static bool         s_started;

static const char *PAGE =
    "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>NEO Music 配网</title><style>"
    "body{background:#0e0f11;color:#eee7d8;font-family:system-ui;padding:24px}"
    "h1{font-size:20px;color:#e2a640}input{display:block;width:100%;max-width:320px;"
    "margin:10px 0;padding:12px;background:#22262a;color:#eee7d8;border:1px solid #45464a;"
    "border-radius:8px;font-size:16px}button{margin-top:12px;padding:12px 28px;background:#e2a640;"
    "color:#0e0f11;border:0;border-radius:8px;font-size:16px}</style></head><body>"
    "<h1>连接 WiFi</h1><p>填入路由器名称和密码，保存后设备会自动连接。</p>"
    "<form action=\"/save\" method=\"post\">"
    "<input name=\"s\" placeholder=\"WiFi 名称\" maxlength=\"32\">"
    "<input name=\"p\" type=\"password\" placeholder=\"密码\" maxlength=\"64\">"
    "<button type=\"submit\">保存并连接</button></form></body></html>";

/**
 * 联网后对时。
 *
 * 海棠网关用 Date.now() 做签名时间戳，设备系统时间若停在 1970 年，
 * 服务端会判 ts 非法 → 签名失败 → 播放地址取不到。所以拿到 IP 就同步时间。
 * 失败只记日志，不影响其它功能。
 */
static void start_sntp(void)
{
    static bool started;
    if (started) {
        return;
    }
    started = true;

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
    cfg.start = true;
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP 启动失败: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "SNTP 已启动（ntp.aliyun.com）");
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        s_state = WIFI_CONNECTING;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_state = WIFI_FAILED;
        s_ip[0] = '\0';
        s_rssi = 0;
        if (s_retry < 5) {
            s_retry++;
            esp_wifi_connect();
            s_state = WIFI_CONNECTING;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_state = WIFI_CONNECTED;
        s_retry = 0;
        start_sntp();                     /* 海棠签名需要真实时间 */
        /* 流式播放必须关掉 WiFi 省电：
           省电模式下射频按 DTIM 周期休眠唤醒，读流会出现几十到上百毫秒的延迟尖峰，
           音频缓冲一被打空就「抽搐 / 卡顿」。
           代价是功耗略升，但这台机器一直是插电用的，无所谓。 */
        esp_err_t ps = esp_wifi_set_ps(WIFI_PS_NONE);
        if (ps == ESP_OK) {
            ESP_LOGI(TAG, "WiFi 省电已关闭（流式播放不抽搐）");
        } else {
            ESP_LOGW(TAG, "WiFi 省电关闭失败: %s", esp_err_to_name(ps));
        }
        if (s_connected_cb) {
            s_connected_cb();
        }
        esp_wifi_sta_get_ap_info(NULL);   /* 触发一次 RSSI 刷新 */
        ESP_LOGI(TAG, "已连接，IP %s", s_ip);
    }
}

static esp_err_t page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
}

static void url_decode(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    for (const char *p = src; *p && n + 1 < cap; p++) {
        if (*p == '+') {
            dst[n++] = ' ';
        } else if (*p == '%' && p[1] && p[2]) {
            char hex[3] = { p[1], p[2], 0 };
            dst[n++] = (char)strtol(hex, NULL, 16);
            p += 2;
        } else if (*p == '&') {
            break;
        } else {
            dst[n++] = *p;
        }
    }
    dst[n] = '\0';
}

static esp_err_t save_post(httpd_req_t *req)
{
    char body[256];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "空请求");
    }
    body[len] = '\0';

    char *s = strstr(body, "s=");
    char *p = strstr(body, "p=");
    char ssid[33] = "", pass[65] = "";
    if (s) url_decode(ssid, sizeof(ssid), s + 2);
    if (p) url_decode(pass, sizeof(pass), p + 2);

    if (ssid[0] == '\0') {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "名称不能为空");
    }

    strncpy(g_cfg.wifi_ssid, ssid, sizeof(g_cfg.wifi_ssid) - 1);
    strncpy(g_cfg.wifi_pass, pass, sizeof(g_cfg.wifi_pass) - 1);
    settings_save();

    const char *ok = "<html lang=\"zh-CN\"><meta charset=\"utf-8\">"
                     "<body style=\"background:#0e0f11;color:#eee7d8;padding:24px\">"
                     "已保存，设备正在连接，请回到屏幕查看状态。</body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, ok, HTTPD_RESP_USE_STRLEN);
    wifi_mgr_connect(ssid, pass);
    return ESP_OK;
}

static void start_config_ap(void)
{
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    esp_netif_ip_info_t ip;
    IP4_ADDR(&ip.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(ap);
    esp_netif_set_ip_info(ap, &ip);
    esp_netif_dhcps_start(ap);

    wifi_config_t cfg = { 0 };
    snprintf((char *)cfg.ap.ssid, sizeof(cfg.ap.ssid), "%s", s_ap_ssid);
    cfg.ap.ssid_len = strlen(s_ap_ssid);
    cfg.ap.channel = 1;
    cfg.ap.max_connection = 2;
    cfg.ap.authmode = WIFI_AUTH_OPEN;
    /* APSTA：保持热点的同时还能扫描周围网络，用户可以直接在设备上选 */
    TRY(esp_wifi_set_mode(WIFI_MODE_APSTA));
    TRY(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    TRY(esp_wifi_start());

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.server_port = 80;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &hc) == ESP_OK) {
        httpd_uri_t g = { .uri = "/", .method = HTTP_GET, .handler = page_get };
        httpd_uri_t p = { .uri = "/save", .method = HTTP_POST, .handler = save_post };
        httpd_register_uri_handler(server, &g);
        httpd_register_uri_handler(server, &p);
    }
    s_state = WIFI_AP;
    ESP_LOGI(TAG, "配网热点 %s，浏览器打开 http://192.168.4.1", s_ap_ssid);
}

void wifi_mgr_connect(const char *ssid, const char *pass)
{
    if (!s_started) return;
    TRY(esp_wifi_stop());
    TRY(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t cfg = { 0 };
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    TRY(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    TRY(esp_wifi_start());
    s_state = WIFI_CONNECTING;
    s_retry = 0;
    ESP_LOGI(TAG, "正在连接 %s", ssid);
}

void wifi_mgr_init(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "NEO-Music-%02X%02X", mac[4], mac[5]);

    TRY(esp_netif_init());
    TRY(esp_event_loop_create_default());
    esp_event_handler_instance_t h1, h2;
    TRY(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &on_wifi_event, NULL, &h1));
    TRY(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &on_wifi_event, NULL, &h2));
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    TRY(esp_wifi_init(&ic));
    s_started = true;

    if (g_cfg.wifi_ssid[0]) {
        esp_netif_create_default_wifi_sta();
        wifi_mgr_connect(g_cfg.wifi_ssid, g_cfg.wifi_pass);
    } else {
        /* 配网模式：APSTA，热点开着的同时也能扫周围网络，设备上直接选 */
        esp_netif_create_default_wifi_sta();
        start_config_ap();
    }
}

int wifi_mgr_scan(wifi_ap_t *aps, int max)
{
    if (!aps || max <= 0) {
        return 0;
    }

    /* IDF 5.3 的 wifi_scan_config_t 只有这几个字段，没有 active/passive */
    wifi_scan_config_t sc;
    memset(&sc, 0, sizeof(sc));
    sc.show_hidden = false;

    /* esp_wifi_scan_stop() 是异步的，不停稳就立刻 start 会返回 NOT_STOPPED */
    esp_wifi_scan_stop();
    vTaskDelay(pdMS_TO_TICKS(200));

    esp_err_t err = esp_wifi_scan_start(&sc, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "扫描启动失败: %s", esp_err_to_name(err));
        return 0;
    }

    uint16_t num = 0;
    if (esp_wifi_scan_get_ap_num(&num) != ESP_OK || num == 0) {
        return 0;
    }
    if (num > (uint16_t)max) {
        num = (uint16_t)max;
    }

    wifi_ap_record_t *recs = calloc(num, sizeof(wifi_ap_record_t));
    if (!recs) {
        return 0;
    }
    if (esp_wifi_scan_get_ap_records(&num, recs) != ESP_OK) {
        free(recs);
        return 0;
    }

    int cnt = 0;
    for (uint16_t i = 0; i < num && cnt < max; i++) {
        if (!recs[i].ssid[0]) {
            continue;
        }
        /* 同名 SSID（不同频段）只留信号最强的那个 */
        bool dup = false;
        for (int k = 0; k < cnt; k++) {
            if (strcmp(aps[k].ssid, (const char *)recs[i].ssid) == 0) {
                dup = true;
                if (recs[i].rssi > aps[k].rssi) {
                    aps[k].rssi = recs[i].rssi;
                }
                break;
            }
        }
        if (dup) {
            continue;
        }
        strncpy(aps[cnt].ssid, (const char *)recs[i].ssid, sizeof(aps[cnt].ssid) - 1);
        aps[cnt].ssid[sizeof(aps[cnt].ssid) - 1] = '\0';
        aps[cnt].rssi   = recs[i].rssi;
        aps[cnt].locked = (recs[i].authmode != WIFI_AUTH_OPEN);
        cnt++;
    }
    free(recs);

    /* 强信号排前面 */
    for (int i = 0; i < cnt - 1; i++) {
        for (int j = i + 1; j < cnt; j++) {
            if (aps[j].rssi > aps[i].rssi) {
                wifi_ap_t t = aps[i];
                aps[i] = aps[j];
                aps[j] = t;
            }
        }
    }
    return cnt;
}

void wifi_mgr_save(const char *ssid, const char *pass)
{
    if (!ssid) return;
    strncpy(g_cfg.wifi_ssid, ssid, sizeof(g_cfg.wifi_ssid) - 1);
    g_cfg.wifi_ssid[sizeof(g_cfg.wifi_ssid) - 1] = '\0';
    strncpy(g_cfg.wifi_pass, pass ? pass : "", sizeof(g_cfg.wifi_pass) - 1);
    g_cfg.wifi_pass[sizeof(g_cfg.wifi_pass) - 1] = '\0';
    settings_save();
    ESP_LOGI(TAG, "已保存网络 %s", g_cfg.wifi_ssid);
}

void wifi_mgr_set_connected_callback(void (*cb)(void))
{
    s_connected_cb = cb;
}

void wifi_mgr_forget(void)
{
    g_cfg.wifi_ssid[0] = '\0';
    g_cfg.wifi_pass[0] = '\0';
    settings_save();
}

wifi_state_t wifi_mgr_state(void)
{
    if (s_state == WIFI_CONNECTED) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            s_rssi = ap.rssi;
        }
    }
    return s_state;
}

int wifi_mgr_rssi(void)
{
    return s_rssi;
}

const char *wifi_mgr_ip(void)
{
    return s_ip;
}

const char *wifi_mgr_ap_ssid(void)
{
    return s_ap_ssid;
}

const char *wifi_mgr_state_text(void)
{
    switch (wifi_mgr_state()) {
    case WIFI_AP:         return "配网热点";
    case WIFI_CONNECTING: return "正在连接";
    case WIFI_CONNECTED:  return "已连接";
    case WIFI_FAILED:     return "连接失败";
    default:              return "未连接";
    }
}
