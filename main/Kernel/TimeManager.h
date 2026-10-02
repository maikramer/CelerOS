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
    // false = campos fora da faixa (ano 2020..2099); hora nao muda
    static bool setManualTime(int year, int month, int day, int hour, int minute);
    // false = TZ vazio, longo (>48) ou com '|'/controle (o config_time.txt
    // separa campos por '|'); fuso nao muda
    static bool setTimezone(const std::string& tzOffset);
    static void setTimeFormat(bool use24h);
    static void setNTPEnabled(bool enabled);

    // Alarme (API 12, em RAM — nao sobrevive a reboot): dispara na PROXIMA
    // ocorrencia de hh:mm (hoje se ainda nao passou, senao amanha) e se
    // desarma: toast no launcher, banner + bipe com app aberto (pollAlarm).
    static bool setAlarm(int hour, int minute, const std::string& msg);
    static void clearAlarm();
    // false = desarmado; senao preenche hora/minuto/mensagem
    static bool getAlarm(int& hour, int& minute, std::string& msg);
    // true UMA vez quando o alarme vence (desarma e preenche a mensagem).
    // Chamado pelo tick do launcher e pelo present() dos apps JS — com app
    // aberto o alarme tambem dispara (banner na faixa + bipe).
    static bool pollAlarm(std::string& msg);

    // Getters
    static std::string getFormattedTime();
    static std::string getFormattedDate();
    static int getYear();
    static int getMonth();
    static int getDay();
    static int getSeconds();
    static int getWeekday();   // 0=domingo..6=sabado (API 13: System.getWeekday)

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
