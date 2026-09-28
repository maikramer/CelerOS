/**
 * @file ScopedLock.h
 * @brief RAII-based lock management for FreeRTOS mutexes
 * @author maikeu
 * @date 2024
 * 
 * Provides automatic lock/unlock via RAII pattern, preventing deadlocks
 * from forgotten unlock calls.
 */

#ifndef SCOPED_LOCK_H
#define SCOPED_LOCK_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <utility>

namespace SafeContainers {

/**
 * @class ScopedLock
 * @brief RAII wrapper for FreeRTOS mutex that automatically releases on destruction.
 * 
 * Usage:
 * @code
 * SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
 * {
 *     ScopedLock lock(mutex);
 *     if (lock.isLocked()) {
 *         // Safe access to shared resource
 *     }
 * } // Automatically unlocked here
 * @endcode
 */
class ScopedLock {
public:
    /**
     * @brief Constructs a ScopedLock and attempts to acquire the mutex.
     * @param mutex The FreeRTOS semaphore handle.
     * @param timeoutTicks Maximum ticks to wait for lock (default: 1000ms).
     */
    explicit ScopedLock(SemaphoreHandle_t mutex, TickType_t timeoutTicks = pdMS_TO_TICKS(1000))
        : _mutex(mutex), _locked(false) {
        if (_mutex != nullptr) {
            _locked = (xSemaphoreTake(_mutex, timeoutTicks) == pdTRUE);
            if (!_locked) {
                ESP_LOGW("ScopedLock", "Failed to acquire lock within timeout");
            }
        }
    }

    /**
     * @brief Destructor - automatically releases the mutex if held.
     */
    ~ScopedLock() {
        unlock();
    }

    // Non-copyable
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;

    // Movable
    ScopedLock(ScopedLock&& other) noexcept
        : _mutex(other._mutex), _locked(other._locked) {
        other._mutex = nullptr;
        other._locked = false;
    }

    ScopedLock& operator=(ScopedLock&& other) noexcept {
        if (this != &other) {
            unlock();
            _mutex = other._mutex;
            _locked = other._locked;
            other._mutex = nullptr;
            other._locked = false;
        }
        return *this;
    }

    /**
     * @brief Checks if the lock was successfully acquired.
     * @return true if locked, false otherwise.
     */
    [[nodiscard]] bool isLocked() const { return _locked; }

    /**
     * @brief Explicit conversion to bool for use in conditions.
     * @return true if locked.
     */
    explicit operator bool() const { return _locked; }

    /**
     * @brief Manually unlock before destruction.
     */
    void unlock() {
        if (_locked && _mutex != nullptr) {
            xSemaphoreGive(_mutex);
            _locked = false;
        }
    }

    /**
     * @brief Try to reacquire the lock after manual unlock.
     * @param timeoutTicks Maximum ticks to wait.
     * @return true if lock acquired.
     */
    bool tryLock(TickType_t timeoutTicks = pdMS_TO_TICKS(1000)) {
        if (_locked) return true;
        if (_mutex != nullptr) {
            _locked = (xSemaphoreTake(_mutex, timeoutTicks) == pdTRUE);
        }
        return _locked;
    }

private:
    SemaphoreHandle_t _mutex;
    bool _locked;
};

/**
 * @class RecursiveScopedLock
 * @brief RAII wrapper for FreeRTOS recursive mutex.
 * 
 * Use when the same task may need to acquire the lock multiple times.
 */
class RecursiveScopedLock {
public:
    explicit RecursiveScopedLock(SemaphoreHandle_t mutex, TickType_t timeoutTicks = pdMS_TO_TICKS(1000))
        : _mutex(mutex), _locked(false) {
        if (_mutex != nullptr) {
            _locked = (xSemaphoreTakeRecursive(_mutex, timeoutTicks) == pdTRUE);
            if (!_locked) {
                ESP_LOGW("RecursiveScopedLock", "Failed to acquire recursive lock within timeout");
            }
        }
    }

    ~RecursiveScopedLock() {
        unlock();
    }

    RecursiveScopedLock(const RecursiveScopedLock&) = delete;
    RecursiveScopedLock& operator=(const RecursiveScopedLock&) = delete;

    RecursiveScopedLock(RecursiveScopedLock&& other) noexcept
        : _mutex(other._mutex), _locked(other._locked) {
        other._mutex = nullptr;
        other._locked = false;
    }

    RecursiveScopedLock& operator=(RecursiveScopedLock&& other) noexcept {
        if (this != &other) {
            unlock();
            _mutex = other._mutex;
            _locked = other._locked;
            other._mutex = nullptr;
            other._locked = false;
        }
        return *this;
    }

    [[nodiscard]] bool isLocked() const { return _locked; }
    explicit operator bool() const { return _locked; }

    void unlock() {
        if (_locked && _mutex != nullptr) {
            xSemaphoreGiveRecursive(_mutex);
            _locked = false;
        }
    }

private:
    SemaphoreHandle_t _mutex;
    bool _locked;
};

} // namespace SafeContainers

#endif // SCOPED_LOCK_H
