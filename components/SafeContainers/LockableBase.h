/**
 * @file LockableBase.h
 * @brief Base class for thread-safe containers with mutex protection
 * @author maikeu
 * @date 2024
 * 
 * Provides a base class with mutex management for derived thread-safe containers.
 */

#ifndef LOCKABLE_BASE_H
#define LOCKABLE_BASE_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ScopedLock.h"
#include "esp_log.h"

namespace SafeContainers {

/**
 * @class LockableBase
 * @brief Base class providing mutex functionality for thread-safe containers.
 * 
 * This class manages a FreeRTOS mutex and provides protected lock/unlock methods
 * for derived classes. It's designed to be used with RAII-style locking via ScopedLock.
 */
class LockableBase {
public:
    /**
     * @brief Default constructor - creates the mutex.
     */
    LockableBase() : _mutex(xSemaphoreCreateMutex()) {
        if (_mutex == nullptr) {
            ESP_LOGE("LockableBase", "Failed to create mutex!");
        }
    }

    /**
     * @brief Destructor - deletes the mutex.
     */
    virtual ~LockableBase() {
        if (_mutex != nullptr) {
            vSemaphoreDelete(_mutex);
            _mutex = nullptr;
        }
    }

    // Non-copyable to prevent mutex sharing issues
    LockableBase(const LockableBase&) = delete;
    LockableBase& operator=(const LockableBase&) = delete;

    // Movable - transfers mutex ownership
    LockableBase(LockableBase&& other) noexcept : _mutex(other._mutex) {
        other._mutex = nullptr;
    }

    LockableBase& operator=(LockableBase&& other) noexcept {
        if (this != &other) {
            if (_mutex != nullptr) {
                vSemaphoreDelete(_mutex);
            }
            _mutex = other._mutex;
            other._mutex = nullptr;
        }
        return *this;
    }

    /**
     * @brief Creates a scoped lock for this container.
     * @param timeoutMs Timeout in milliseconds (default: 1000).
     * @return ScopedLock instance - check isLocked() before using.
     * 
     * Usage:
     * @code
     * {
     *     auto lock = container.lock();
     *     if (lock) {
     *         // Safe access
     *     }
     * }
     * @endcode
     */
    [[nodiscard]] ScopedLock lock(uint32_t timeoutMs = 1000) const {
        return ScopedLock(_mutex, pdMS_TO_TICKS(timeoutMs));
    }

    /**
     * @brief Checks if the mutex is valid.
     * @return true if mutex was created successfully.
     */
    [[nodiscard]] bool isValid() const {
        return _mutex != nullptr;
    }

protected:
    /**
     * @brief Gets the raw mutex handle for derived classes.
     * @return The FreeRTOS semaphore handle.
     */
    [[nodiscard]] SemaphoreHandle_t getMutex() const { return _mutex; }

    /**
     * @brief Legacy lock method - prefer using lock() with ScopedLock.
     * @param timeoutMs Timeout in milliseconds.
     * @return true if lock acquired.
     * @deprecated Use lock() method returning ScopedLock instead.
     */
    [[nodiscard]] bool acquireLock(uint32_t timeoutMs = 1000) const {
        if (_mutex == nullptr) return false;
        return xSemaphoreTake(_mutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
    }

    /**
     * @brief Legacy unlock method - prefer using ScopedLock.
     * @deprecated Use ScopedLock instead.
     */
    void releaseLock() const {
        if (_mutex != nullptr) {
            xSemaphoreGive(_mutex);
        }
    }

private:
    mutable SemaphoreHandle_t _mutex;
};

/**
 * @class RecursiveLockableBase
 * @brief Base class with recursive mutex for containers that may reenter.
 */
class RecursiveLockableBase {
public:
    RecursiveLockableBase() : _mutex(xSemaphoreCreateRecursiveMutex()) {
        if (_mutex == nullptr) {
            ESP_LOGE("RecursiveLockableBase", "Failed to create recursive mutex!");
        }
    }

    virtual ~RecursiveLockableBase() {
        if (_mutex != nullptr) {
            vSemaphoreDelete(_mutex);
            _mutex = nullptr;
        }
    }

    RecursiveLockableBase(const RecursiveLockableBase&) = delete;
    RecursiveLockableBase& operator=(const RecursiveLockableBase&) = delete;

    RecursiveLockableBase(RecursiveLockableBase&& other) noexcept : _mutex(other._mutex) {
        other._mutex = nullptr;
    }

    RecursiveLockableBase& operator=(RecursiveLockableBase&& other) noexcept {
        if (this != &other) {
            if (_mutex != nullptr) {
                vSemaphoreDelete(_mutex);
            }
            _mutex = other._mutex;
            other._mutex = nullptr;
        }
        return *this;
    }

    [[nodiscard]] RecursiveScopedLock lock(uint32_t timeoutMs = 1000) const {
        return RecursiveScopedLock(_mutex, pdMS_TO_TICKS(timeoutMs));
    }

    [[nodiscard]] bool isValid() const {
        return _mutex != nullptr;
    }

protected:
    [[nodiscard]] SemaphoreHandle_t getMutex() const { return _mutex; }

private:
    mutable SemaphoreHandle_t _mutex;
};

} // namespace SafeContainers

#endif // LOCKABLE_BASE_H
