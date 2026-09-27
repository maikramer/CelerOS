#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <Arduino.h>
#include <time.h>
#include <string>

class TimeManager {
public:
    static void init();
    static void syncNTP();
    static void setManualTime(int year, int month, int day, int hour, int minute);
    static void setTimezone(const std::string& tzOffset);
    static void setTimeFormat(bool use24h);
    static void setNTPEnabled(bool enabled);
    
    // Getters
    static std::string getFormattedTime();
    static std::string getFormattedDate();
    static int getYear();
    static int getMonth();
    static int getDay();
    static int getSeconds();
    
    // Preferences state
    static std::string currentTimezone;
    static bool use24hFormat;
    static bool ntpEnabled;

private:
    static void savePreferences();
    static void loadPreferences();
};

#endif // TIME_MANAGER_H
