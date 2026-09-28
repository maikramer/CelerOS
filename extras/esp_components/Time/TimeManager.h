#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <string>
#include <ctime>
#include <vector>
#include "Event.h"

/**
 * @file TimeManager.h
 * @brief NTP time synchronization and timezone management.
 * 
 * This singleton class handles SNTP time synchronization with automatic
 * retry, multiple NTP server fallback, and timezone configuration.
 */

/**
 * @enum TimeSyncStatus
 * @brief Status of time synchronization.
 */
enum class TimeSyncStatus {
    NotStarted,     /**< Sync not yet attempted */
    InProgress,     /**< Sync in progress */
    Completed,      /**< Sync completed successfully */
    Failed          /**< Sync failed */
};

/**
 * @struct TimeConfig
 * @brief Configuration for TimeManager.
 */
struct TimeConfig {
    std::string primaryServer;          /**< Primary NTP server */
    std::string secondaryServer;        /**< Secondary NTP server (fallback) */
    std::string tertiaryServer;         /**< Tertiary NTP server (fallback) */
    std::string timezone;               /**< Timezone string (POSIX format) */
    uint32_t syncIntervalSeconds;       /**< Auto-resync interval (0 = disabled) */
    uint32_t syncTimeoutMs;             /**< Timeout for sync operation */
    uint8_t maxRetries;                 /**< Maximum sync retry attempts */
    
    TimeConfig() :
        primaryServer("pool.ntp.org"),
        secondaryServer("time.google.com"),
        tertiaryServer("time.cloudflare.com"),
        timezone("UTC0"),
        syncIntervalSeconds(3600),  // 1 hour
        syncTimeoutMs(15000),       // 15 seconds
        maxRetries(3) {}
};

/**
 * @class TimeManager
 * @brief Singleton class for time synchronization and management.
 * 
 * Usage:
 * @code
 * auto& time = TimeManager::instance();
 * time.init("pool.ntp.org");
 * time.setTimezone("BRT3");  // Brazil timezone
 * 
 * if (time.isSynced()) {
 *     ESP_LOGI(TAG, "Time: %s", time.getFormattedTime().c_str());
 * }
 * @endcode
 */
class TimeManager {
public:
    /**
     * @brief Get the singleton instance.
     * @return Reference to the TimeManager instance.
     */
    static TimeManager& instance();

    /**
     * @brief Initialize with default NTP server.
     * @param ntpServer NTP server address (default: pool.ntp.org).
     * @return True if initialization successful.
     */
    bool init(const std::string& ntpServer = "pool.ntp.org");

    /**
     * @brief Initialize with full configuration.
     * @param config TimeConfig structure.
     * @return True if initialization successful.
     */
    bool init(const TimeConfig& config);

    /**
     * @brief Deinitialize and stop SNTP.
     */
    void deinit();

    /**
     * @brief Check if manager is initialized.
     * @return True if initialized.
     */
    bool isInitialized() const { return _initialized; }

    // ========== Synchronization ==========

    /**
     * @brief Force a time synchronization.
     * @param blocking If true, wait until sync completes or times out.
     * @return True if sync started (or completed if blocking).
     */
    bool sync(bool blocking = true);

    /**
     * @brief Check if time is synchronized.
     * @return True if time has been synced at least once.
     */
    bool isSynced() const { return _synced; }

    /**
     * @brief Get synchronization status.
     * @return Current TimeSyncStatus.
     */
    TimeSyncStatus getSyncStatus() const { return _syncStatus; }

    /**
     * @brief Get time of last successful sync.
     * @return time_t of last sync (0 if never synced).
     */
    time_t getLastSyncTime() const { return _lastSyncTime; }

    // ========== Timezone ==========

    /**
     * @brief Set timezone using POSIX format.
     * @param tz Timezone string (e.g., "BRT3", "EST5EDT", "UTC0").
     * 
     * Common timezones:
     * - "BRT3" - Brazil (no DST)
     * - "BRT3BRST,M10.3.0/0,M2.3.0/0" - Brazil with DST
     * - "EST5EDT,M3.2.0,M11.1.0" - US Eastern
     * - "PST8PDT,M3.2.0,M11.1.0" - US Pacific
     * - "CET-1CEST,M3.5.0,M10.5.0/3" - Central Europe
     * - "UTC0" - UTC
     */
    void setTimezone(const std::string& tz);

    /**
     * @brief Get current timezone string.
     * @return Current timezone.
     */
    std::string getTimezone() const { return _config.timezone; }

    // ========== Time Retrieval ==========

    /**
     * @brief Get current time as time_t.
     * @return Current time (0 if not synced).
     */
    time_t getTime() const;

    /**
     * @brief Get current time as struct tm (local time).
     * @return struct tm with local time.
     */
    struct tm getLocalTime() const;

    /**
     * @brief Get current time as struct tm (UTC).
     * @return struct tm with UTC time.
     */
    struct tm getUtcTime() const;

    /**
     * @brief Get formatted time string.
     * @param format strftime format string (default: "%Y-%m-%d %H:%M:%S").
     * @return Formatted time string.
     */
    std::string getFormattedTime(const char* format = "%Y-%m-%d %H:%M:%S") const;

    /**
     * @brief Get formatted date string.
     * @param format strftime format string (default: "%Y-%m-%d").
     * @return Formatted date string.
     */
    std::string getFormattedDate(const char* format = "%Y-%m-%d") const;

    /**
     * @brief Get ISO 8601 formatted timestamp.
     * @return ISO 8601 string (e.g., "2024-01-15T14:30:00-03:00").
     */
    std::string getIsoTimestamp() const;

    /**
     * @brief Get Unix timestamp (seconds since epoch).
     * @return Unix timestamp.
     */
    uint64_t getUnixTimestamp() const;

    /**
     * @brief Get Unix timestamp in milliseconds.
     * @return Unix timestamp in milliseconds.
     */
    uint64_t getUnixTimestampMs() const;

    // ========== Utility ==========

    /**
     * @brief Check if current year is valid (>2020).
     * @return True if time appears to be valid.
     */
    bool isTimeValid() const;

    /**
     * @brief Get current configuration.
     * @return TimeConfig structure.
     */
    const TimeConfig& getConfig() const { return _config; }

    /**
     * @brief Set NTP servers.
     * @param primary Primary server.
     * @param secondary Secondary server (optional).
     * @param tertiary Tertiary server (optional).
     */
    void setNtpServers(const std::string& primary,
                       const std::string& secondary = "",
                       const std::string& tertiary = "");

    // ========== Events ==========

    /**
     * @brief Event triggered when time is synchronized.
     * Parameter: time_t of the synchronized time.
     */
    Event<time_t> onSynced;

    /**
     * @brief Event triggered when sync fails.
     * Parameter: error message.
     */
    Event<const std::string&> onSyncFailed;

private:
    TimeManager();
    ~TimeManager();
    TimeManager(const TimeManager&) = delete;
    TimeManager& operator=(const TimeManager&) = delete;

    /**
     * @brief SNTP callback for time sync notification.
     */
    static void timeSyncNotificationCallback(struct timeval* tv);

    /**
     * @brief Apply timezone setting.
     */
    void applyTimezone();

    TimeConfig _config;
    TimeSyncStatus _syncStatus;
    time_t _lastSyncTime;
    bool _initialized;
    bool _synced;

    static TimeManager* _instance;  // For callback access
    static constexpr const char* TAG = "TimeManager";
};

#endif // TIME_MANAGER_H
