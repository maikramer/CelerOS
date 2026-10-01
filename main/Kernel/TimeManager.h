#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <Arduino.h>
#include <time.h>
#include <string>

class TimeManager {
public:
    static void init();
    static void syncNTP();
    // Retentativa de SNTP enquanto a hora for invalida (chamar no loop);
    // tambem persiste a hora (a cada 10 min) e dispara o alarme do dia
    static void tick(bool networkUp);
    static bool isTimeValid();
    static void setManualTime(int year, int month, int day, int hour, int minute);
    static void setTimezone(const std::string& tzOffset);
    static void setTimeFormat(bool use24h);
    static void setNTPEnabled(bool enabled);

    // Alarme do dia (API 12, em RAM — nao sobrevive a reboot): quando o
    // relogio local passa de hh:mm dispara um toast e se desarma.
    static bool setAlarm(int hour, int minute, const std::string& msg);
    static void clearAlarm();
    // {armed,hour,minute,msg} serializado em JSON simples p/ o JS
    static std::string getAlarmJson();

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
    static void saveEpoch();
    static void restoreEpoch();
};

#endif // TIME_MANAGER_H
