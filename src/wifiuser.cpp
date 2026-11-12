/**
 * @file wifiuser.cpp
 * @brief WiFi connection management
 */
#include "wifiuser.h"
#include "ui.h"
#include "data.h"
#include <WiFi.h>

void WiFi_update()
{
    if (Set.WiFi_connect_flag) {
        WiFi_connect();
        Set.WiFi_connect_flag = false;
    }

    if (Set.WiFi_connect_stauts || WiFi.status() == WL_CONNECTED) {
        Set.WiFi_connect_stauts = true;
        ui_set_wifi_status(true);
    } else {
        ui_set_wifi_status(false);
    }
}

void WiFi_init()
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    Serial.println("[WiFi] Initialized");
}

void WiFi_connect()
{
    Serial.printf("[WiFi] Connecting to %s...\n", Set.WiFi_name);
    WiFi.begin(Set.WiFi_name, Set.WiFi_password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Set.WiFi_connect_stauts = true;
        Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        ui_set_wifi_status(true);
        ui_show_toast("WiFi\xe5\xb7\xb2\xe8\xbf\x9e\xe6\x8e\xa5");
        time_get();
    } else {
        Set.WiFi_connect_stauts = false;
        Serial.println("[WiFi] Connection failed");
        ui_set_wifi_status(false);
        ui_show_toast("WiFi\xe8\xbf\x9e\xe6\x8e\xa5\xe5\xa4\xb1\xe8\xb4\xa5");
    }
}
