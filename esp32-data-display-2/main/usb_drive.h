#pragma once

#include <stdbool.h>

#include "esp_err.h"

/**
 * Initialize SD card and USB MSC device mode.
 *
 * Initializes the SDMMC host, probes the card, registers it with TinyUSB
 * MSC storage, mounts FATFS at BSP_SD_MOUNT_POINT for app use, and installs
 * the TinyUSB driver.
 *
 * When a PC accesses the drive, the VFS is automatically unmounted from the
 * app and any active logging is stopped. When the PC ejects, VFS is remounted.
 */
esp_err_t dd_usb_drive_init(void);

/**
 * Returns true when the USB host (PC) is actively accessing the SD card.
 * While true, no application code should access the SD card mount point.
 */
bool dd_usb_drive_is_host_connected(void);
