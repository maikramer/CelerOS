#ifndef SDCARD_H
#define SDCARD_H

#include "projectConfig.h"
#define USE_SDCARD
#ifdef USE_SDCARD
#include <Storage.h>
#include <esp_log.h>
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "CommonErrorCodes.h"

/**
 * @file SdCard.h
 * @brief Defines the SdCard class for initializing and mounting an SD card.
 */

/**
 * @class SdCard
 * @brief Handles the initialization and mounting of an SD card using the SPI interface.
 */
class SdCard {
public:
    /**
     * @brief Initializes and mounts the SD card.
     * 
     * This function attempts to mount the SD card at the specified mount point. If the SD card
     * is not present or mounting fails, it will return an error code.
     *
     * @param mountPoint The path where the SD card should be mounted (e.g., "/sdcard").
     * @param formatIfFailed If true, the SD card will be formatted if mounting fails. Be careful:
     *                       formatting will erase all existing data on the SD card.
     * @return ErrorCode indicating the result of the initialization process.
     *         CommonErrorCodes::None indicates success.
     */
    static ErrorCode initialize(const char* mountPoint, bool formatIfFailed = false);
};

#endif // USE_SDCARD

#endif // SDCARD_H