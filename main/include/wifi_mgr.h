/*
 * wifi_mgr.h —— WiFi 连接与配网
 *
 * 状态一律取真实值：没连上就是不显示信号格，不画假的满格信号。
 * 没保存过 SSID 时开 AP 配网热点，浏览器打开 192.168.4.1 填中文页面。
 */
#pragma once

#include <stdint.h>

typedef enum {
    WIFI_OFF = 0,
    WIFI_AP,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_FAILED,
} wifi_state_t;

void wifi_mgr_init(void);
void wifi_mgr_connect(const char *ssid, const char *pass);
wifi_state_t wifi_mgr_state(void);
int  wifi_mgr_rssi(void);
const char *wifi_mgr_ip(void);
const char *wifi_mgr_ap_ssid(void);
const char *wifi_mgr_state_text(void);
