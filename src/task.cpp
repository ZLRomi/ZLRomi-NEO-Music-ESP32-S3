/**
 * @file task.cpp
 * @brief FreeRTOS tasks for music, WiFi, data updates
 */
#include "task.h"
#include "ui.h"
#include "wifiuser.h"
#include "data.h"
#include "sduser.h"
#include "music.h"

TaskHandle_t WiFiHandle = nullptr;
TaskHandle_t serialHandle = nullptr;

void task_init()
{
    // Music task on core 0
    xTaskCreatePinnedToCore(music_task, "music_task", 1024*10, NULL, 1, NULL, 0);
    // Data task on core 0
    xTaskCreatePinnedToCore(data_task, "data_task", 1024*5, NULL, 1, NULL, 0);
}

void WiFiTask_create()
{
    if (WiFiHandle == nullptr) {
        xTaskCreatePinnedToCore(WiFi_task, "WiFi_task", 1024*5, NULL, 1, &WiFiHandle, 0);
    }
}

void serialTask_create()
{
    if (serialHandle == nullptr) {
        xTaskCreatePinnedToCore(serial_task, "serial_task", 1024*5, NULL, 1, &serialHandle, 0);
    }
}

void WiFiTask_delete()
{
    if (WiFiHandle != nullptr) {
        Set.WiFi_button_flag = true;
        vTaskDelete(WiFiHandle);
        WiFiHandle = nullptr;
    }
}

void serialTask_delete()
{
    if (serialHandle != nullptr) {
        vTaskDelete(serialHandle);
        serialHandle = nullptr;
    }
}

void WiFi_task(void *pt)
{
    while (1) {
        WiFi_update();
        vTaskDelay(50);
    }
}

void data_task(void *pt)
{
    while (1) {
        data_update();
        vTaskDelay(1000);
    }
}

void serial_task(void *pt)
{
    while (1) {
        if (Serial.available()) {
            String rx = Serial.readStringUntil('\n');
            Uart.rx_data = rx.c_str();
            Serial.printf("[UART] RX: %s\n", Uart.rx_data);
        }
        vTaskDelay(50);
    }
}

void music_task(void *pt)
{
    while (1) {
        music_update();
        vTaskDelay(5);
    }
}
