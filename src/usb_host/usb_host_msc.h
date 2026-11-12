/**
 * @file usb_host_msc.h
 * @brief USB Host MSC wrapper for ESP32-S3
 * Based on nathalislight/esp32s3_USB_Drive
 * Mounts USB flash drive at /usb/ using FatFs + VFS
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Initialize USB Host and register MSC client
// Creates a FreeRTOS task to handle USB events
// U盘 will be mounted at /usb/ when connected
void usb_host_init(void);

// Check if USB drive is mounted
bool usb_host_is_mounted(void);

// Get mount path (returns "/usb" if mounted, NULL otherwise)
const char* usb_host_get_mount_path(void);

#ifdef __cplusplus
}
#endif
