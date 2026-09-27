#include "TimeManager.h"

#include "esp_sntp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>
#include <cstdio>
#include <sys/time.h>

// Static instance pointer for callback
TimeManager* TimeManager::_instance = nullptr;

TimeManager::TimeManager() :
    _syncStatus(TimeSyncStatus::NotStarted),
    _lastSyncTime(0),
    _initialized(false),
    _synced(false) {
    _instance = this;
}

TimeManager::~TimeManager() {
    deinit();
    _instance = nullptr;
}

TimeManager& TimeManager::instance() {
    static TimeManager instance;
    return instance;
}

bool TimeManager::init(const std::string& ntpServer) {
    TimeConfig config;
    config.primaryServer = ntpServer;
    return init(config);
}

bool TimeManager::init(const TimeConfig& config) {
    if (_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    _config = config;
    _syncStatus = TimeSyncStatus::NotStarted;

    ESP_LOGI(TAG, "Initializing SNTP with server: %s", _config.primaryServer.c_str());

    // Configure SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    
    // Set primary server
    esp_sntp_setservername(0, _config.primaryServer.c_str());
    
    // Set secondary server if provided
    if (!_config.secondaryServer.empty()) {
        esp_sntp_setservername(1, _config.secondaryServer.c_str());
    }
    
    // Set tertiary server if provided
    if (!_config.tertiaryServer.empty()) {
        esp_sntp_setservername(2, _config.tertiaryServer.c_str());
    }

    // Set time sync callback
    sntp_set_time_sync_notification_cb(timeSyncNotificationCallback);

    // Apply timezone
    applyTimezone();

    // Initialize SNTP
    esp_sntp_init();

    _initialized = true;
    ESP_LOGI(TAG, "SNTP initialized");

    return true;
}

void TimeManager::deinit() {
    if (!_initialized) {
        return;
    }

    esp_sntp_stop();
    _initialized = false;
    _synced = false;
    _syncStatus = TimeSyncStatus::NotStarted;
    
    ESP_LOGI(TAG, "SNTP stopped");
}

bool TimeManager::sync(bool blocking) {
    if (!_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    _syncStatus = TimeSyncStatus::InProgress;

    // Restart SNTP to force immediate sync
    esp_sntp_restart();

    if (!blocking) {
        return true;
    }

    // Wait for sync with timeout
    uint32_t startTime = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    uint32_t timeout = _config.syncTimeoutMs;
    uint8_t retries = 0;

    while (!_synced && retries < _config.maxRetries) {
        uint32_t elapsed = static_cast<uint32_t>(esp_timer_get_time() / 1000) - startTime;
        
        if (elapsed >= timeout) {
            retries++;
            if (retries < _config.maxRetries) {
                ESP_LOGW(TAG, "Sync timeout, retry %d/%d", retries, _config.maxRetries);
                esp_sntp_restart();
                startTime = static_cast<uint32_t>(esp_timer_get_time() / 1000);
            }
            continue;
        }

        // Check sync status
        if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (_synced) {
        ESP_LOGI(TAG, "Time synchronized successfully");
        return true;
    } else {
        _syncStatus = TimeSyncStatus::Failed;
        ESP_LOGE(TAG, "Time sync failed after %d retries", _config.maxRetries);
        onSyncFailed.trigger("Sync timeout");
        return false;
    }
}

void TimeManager::setTimezone(const std::string& tz) {
    _config.timezone = tz;
    applyTimezone();
    ESP_LOGI(TAG, "Timezone set to: %s", tz.c_str());
}

void TimeManager::applyTimezone() {
    setenv("TZ", _config.timezone.c_str(), 1);
    tzset();
}

time_t TimeManager::getTime() const {
    time_t now;
    time(&now);
    return now;
}

struct tm TimeManager::getLocalTime() const {
    time_t now = getTime();
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    return timeinfo;
}

struct tm TimeManager::getUtcTime() const {
    time_t now = getTime();
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    return timeinfo;
}

std::string TimeManager::getFormattedTime(const char* format) const {
    struct tm timeinfo = getLocalTime();
    char buffer[64];
    strftime(buffer, sizeof(buffer), format, &timeinfo);
    return std::string(buffer);
}

std::string TimeManager::getFormattedDate(const char* format) const {
    return getFormattedTime(format);
}

std::string TimeManager::getIsoTimestamp() const {
    struct tm timeinfo = getLocalTime();
    char buffer[32];
    
    // Format: 2024-01-15T14:30:00
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    
    // Add timezone offset
    // Note: For simplicity, we're not calculating the actual offset
    // A more complete implementation would use the timezone info
    return std::string(buffer);
}

uint64_t TimeManager::getUnixTimestamp() const {
    return static_cast<uint64_t>(getTime());
}

uint64_t TimeManager::getUnixTimestampMs() const {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000ULL + 
           static_cast<uint64_t>(tv.tv_usec) / 1000ULL;
}

bool TimeManager::isTimeValid() const {
    struct tm timeinfo = getLocalTime();
    // Consider time valid if year is 2020 or later
    return (timeinfo.tm_year + 1900) >= 2020;
}

void TimeManager::setNtpServers(const std::string& primary,
                                 const std::string& secondary,
                                 const std::string& tertiary) {
    _config.primaryServer = primary;
    _config.secondaryServer = secondary;
    _config.tertiaryServer = tertiary;

    if (_initialized) {
        // Update servers on the fly
        esp_sntp_setservername(0, _config.primaryServer.c_str());
        
        if (!secondary.empty()) {
            esp_sntp_setservername(1, _config.secondaryServer.c_str());
        }
        
        if (!tertiary.empty()) {
            esp_sntp_setservername(2, _config.tertiaryServer.c_str());
        }
    }
}

void TimeManager::timeSyncNotificationCallback(struct timeval* tv) {
    if (_instance == nullptr) {
        return;
    }

    ESP_LOGI(TAG, "Time synchronized!");
    
    _instance->_synced = true;
    _instance->_syncStatus = TimeSyncStatus::Completed;
    _instance->_lastSyncTime = tv->tv_sec;
    
    // Trigger event
    _instance->onSynced.trigger(tv->tv_sec);
}
