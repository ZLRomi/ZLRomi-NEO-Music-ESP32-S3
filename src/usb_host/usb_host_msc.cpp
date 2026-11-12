/**
 * @file usb_host_msc.cpp
 * @brief USB Host MSC wrapper for ESP32-S3
 * Based on nathalislight/esp32s3_USB_Drive (Espressif example)
 */
#include "usb_host_msc.h"

#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "msc_host.h"
#include "msc_host_vfs.h"

// MSC event types from msc_host.h (anonymous enum inside struct)
// In C++ we need to reference them properly
static const int EVT_MSC_CONNECTED = 0;     // MSC_DEVICE_CONNECTED
static const int EVT_MSC_DISCONNECTED = 1;  // MSC_DEVICE_DISCONNECTED

static const char *TAG = "USB_HOST";

typedef enum {
    HOST_NO_CLIENT = 0x1,
    HOST_ALL_FREE = 0x2,
    DEVICE_CONNECTED = 0x4,
    DEVICE_DISCONNECTED = 0x8,
    DEVICE_ADDRESS_MASK = 0xFF0,
} app_event_t;

static EventGroupHandle_t s_usb_flags = NULL;
static msc_host_device_handle_t s_msc_device = NULL;
static bool s_mounted = false;

#define READY_TO_UNINSTALL (HOST_NO_CLIENT | HOST_ALL_FREE)

static void msc_event_cb(const msc_host_event_t *event, void *arg)
{
    if (event->event == EVT_MSC_CONNECTED) {
        ESP_LOGI(TAG, "MSC device connected, addr=%d", event->device.address);
        xEventGroupSetBits(s_usb_flags, DEVICE_CONNECTED | (event->device.address << 4));
    } else if (event->event == EVT_MSC_DISCONNECTED) {
        ESP_LOGI(TAG, "MSC device disconnected");
        xEventGroupSetBits(s_usb_flags, DEVICE_DISCONNECTED);
    }
}

static void handle_usb_events(void *args)
{
    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);

        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
            xEventGroupSetBits(s_usb_flags, HOST_NO_CLIENT);
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            xEventGroupSetBits(s_usb_flags, HOST_ALL_FREE);
        }
    }
}

static void usb_host_task(void *arg)
{
    // Wait for device connection
    EventBits_t bits = xEventGroupWaitBits(s_usb_flags,
        DEVICE_CONNECTED | DEVICE_DISCONNECTED,
        pdTRUE, pdFALSE, portMAX_DELAY);

    if (bits & DEVICE_CONNECTED) {
        uint8_t dev_addr = (bits & DEVICE_ADDRESS_MASK) >> 4;
        ESP_LOGI(TAG, "Device connected at address %d", dev_addr);

        esp_err_t err = msc_host_install_device(dev_addr, &s_msc_device);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "msc_host_install_device failed: %s", esp_err_to_name(err));
            return;
        }

        // Get device info
        msc_host_device_info_t info;
        err = msc_host_get_device_info(s_msc_device, &info);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "USB device: VID=0x%04X PID=0x%04X sectors=%lu size=%lu",
                     info.idVendor, info.idProduct,
                     (unsigned long)info.sector_count, (unsigned long)info.sector_size);
        }

        // Mount FAT filesystem at /usb via VFS
        const esp_vfs_fat_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 8,
            .allocation_unit_size = info.sector_size,
        };
        msc_host_vfs_handle_t vfs_handle;
        err = msc_host_vfs_register(s_msc_device, "/usb", &mount_config, &vfs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "VFS mount failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "USB drive mounted at /usb");
            s_mounted = true;

            // List files
            DIR *dir = opendir("/usb");
            if (dir) {
                struct dirent *entry;
                while ((entry = readdir(dir)) != NULL) {
                    ESP_LOGI(TAG, "  /usb/%s", entry->d_name);
                }
                closedir(dir);
            }
        }
    }

    // Keep task alive to handle disconnection
    while (1) {
        bits = xEventGroupWaitBits(s_usb_flags,
            DEVICE_DISCONNECTED | HOST_NO_CLIENT | HOST_ALL_FREE,
            pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & DEVICE_DISCONNECTED) {
            ESP_LOGI(TAG, "Device disconnected");
            s_mounted = false;
            if (s_msc_device) {
                msc_host_uninstall_device(s_msc_device);
                s_msc_device = NULL;
            }
        }
        if (bits & READY_TO_UNINSTALL) {
            break;
        }
    }

    ESP_LOGI(TAG, "USB host task ending");
    vTaskDelete(NULL);
}

void usb_host_init(void)
{
    s_usb_flags = xEventGroupCreate();

    // Install USB Host Library
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "USB Host installed");

    // Install MSC driver with background task
    const msc_host_driver_config_t msc_config = {
        .create_backround_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = tskNO_AFFINITY,
        .callback = msc_event_cb,
        .callback_arg = NULL,
    };
    err = msc_host_install(&msc_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "msc_host_install failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "MSC driver installed");

    // Create USB event handler task
    xTaskCreate(handle_usb_events, "usb_events", 4096, NULL, 5, NULL);

    // Create USB host application task
    xTaskCreate(usb_host_task, "usb_host", 8192, NULL, 4, NULL);

    ESP_LOGI(TAG, "USB Host MSC initialized, waiting for device...");
}

bool usb_host_is_mounted(void)
{
    return s_mounted;
}

const char* usb_host_get_mount_path(void)
{
    return s_mounted ? "/usb" : NULL;
}
