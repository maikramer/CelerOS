#ifndef FLASH_H
#define FLASH_H

#include "Storage.h"
#include "ErrorCode.h"
#include <esp_log.h>
#include "esp_partition.h"
#include <esp_vfs_fat.h>
#include <driver/spi_common.h>

/**
 * @file Flash.h
 * @brief This file defines the Flash class, which provides a convenient interface 
 *        for accessing the external flash memory on the ESP32.
 */

/**
 * @class Flash
 * @brief Provides an interface for interacting with external flash memory (usually attached via SPI).
 */
class Flash {
public:
    /**
     * @brief Initializes the flash memory for file system access.
     *
     * This method attempts to mount the flash memory using the FATFS file system.
     * If the flash is not formatted or mounting fails, it attempts to format the flash.
     *
     * @return ErrorCode indicating the result of the initialization process. 
     *         CommonErrorCodes::None indicates success.
     */
    static ErrorCode initialize();

    /**
     * @brief Gets the handle to the wear levelling driver.
     *
     * @return The wear levelling handle (wl_handle_t) if initialization was successful,
     *         otherwise `WL_INVALID_HANDLE`. 
     */
    static wl_handle_t getWearLevellingHandle();

private:
    static wl_handle_t _wearHandle;  /**< Handle to the wear levelling driver. */
    static bool _initialized;        /**< Flag indicating whether the flash has been initialized. */

    /**
     * @brief Attempts to mount the flash memory with FATFS.
     *
     * @param partitionLabel The label of the flash partition to mount.
     * @param mountPoint The mount point path (where the flash will be accessible).
     * @return ErrorCode indicating the result of the mounting operation.
     */
    static ErrorCode mountFlash(const char* partitionLabel, const char* mountPoint);
};

#endif // FLASH_H