#include "TimeManager.h"
#include "../FileSystem/FileSystem.h"
#include "../Utils/StrUtils.h"
#include "../Utils/CelerSettings.h"
#include "../UI/Kui.h"
#include "../Display/Theme.h"
#include <esp_sntp.h>
#include "esp_netif_sntp.h"

std::string TimeManager::currentTimezone = "UTC0";
bool TimeManager::use24hFormat = true;
bool TimeManager::ntpEnabled = true;

static const char* PREF_FILE = "/local/config_time.txt";

void TimeManager::loadPreferences() {
    if (FileSystem::exists(PREF_FILE)) {
        std::string content = FileSystem::readTextFile(PREF_FILE);
        // Format: TZ|24H|NTP (e.g. "UTC-5|1|1")
        int sep1 = kstr::indexOf(content, '|');
        int sep2 = kstr::indexOf(content, '|', sep1 + 1);
        if (sep1 != -1 && sep2 != -1) {
            currentTimezone = content.substr(0, sep1);
            use24hFormat = (content.substr(sep1 + 1, sep2 - sep1 - 1) == "1");
            ntpEnabled = (content.substr(sep2 + 1) == "1");
        }
    }
}

void TimeManager::savePreferences() {
    std::string content = currentTimezone + "|" + (use24hFormat ? "1" : "0") + "|" + (ntpEnabled ? "1" : "0");
    FileSystem::writeTextFile(PREF_FILE, content.c_str());
}

void TimeManager::init() {
    loadPreferences();
    setenv("TZ", currentTimezone.c_str(), 1);
    tzset();
    // Sem RTC externo, a hora morria em todo reboot sem rede: recupera o
    // ultimo epoch salvo (a cada 10 min no tick) — precisao de minutos, nao
    // de segundos (o uptime desde o save nao e contado).
    restoreEpoch();
    // Defer esp_sntp_init() until syncNTP() to prevent LwIP assertions
}

// SNTP via esp_netif_sntp (thread-safe: o syncNTP e chamado da task de
// eventos de rede). O esp_sntp_stop/init cru rodava fora do lock do lwIP e,
// em alguns boots, a hora nunca sincronizava (relogio parado em 00:00).
static bool s_sntpStarted = false;

void TimeManager::syncNTP() {
    if (!ntpEnabled) return;
    if (!s_sntpStarted) {
        esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        cfg.start = true;
        if (esp_netif_sntp_init(&cfg) == ESP_OK) s_sntpStarted = true;
        return;
    }
    esp_netif_sntp_start();  // reconexao: pede nova sincronizacao ja
}

bool TimeManager::isTimeValid() {
    return getYear() >= 2020;
}

// ---- Persistencia aproximada da hora (sem RTC externo) ---------------------
// O tick grava o epoch no NVS a cada 10 min; o boot recupera se a hora
// ainda for invalida. Drift = tempo desligado (minutos, tipicamente).

void TimeManager::saveEpoch() {
    time_t now;
    time(&now);
    if (now < 1577836800) return;  // < 2020: nao persistir lixo
    char buf[24];  // epoch em segundos cabe folgado (int64 max = 20 digitos)
    snprintf(buf, sizeof(buf), "%lld", (long long)now);
    CelerSettings::set("epoch", buf);
}

void TimeManager::restoreEpoch() {
    if (isTimeValid()) return;
    std::string v = CelerSettings::get("epoch", "");
    if (v.empty()) return;
    long long e = atoll(v.c_str());
    if (e < 1577836800) return;
    struct timeval now = { .tv_sec = (time_t)e, .tv_usec = 0 };
    settimeofday(&now, NULL);
}

// ---- Alarme do dia (API 12) -------------------------------------------------

static bool s_alarmArmed = false;
static int s_alarmH = 0, s_alarmM = 0;
static std::string s_alarmMsg;

bool TimeManager::setAlarm(int hour, int minute, const std::string& msg) {
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
    s_alarmArmed = true;
    s_alarmH = hour;
    s_alarmM = minute;
    s_alarmMsg = msg;
    return true;
}

void TimeManager::clearAlarm() {
    s_alarmArmed = false;
    s_alarmMsg.clear();
}

std::string TimeManager::getAlarmJson() {
    if (!s_alarmArmed) return "null";
    std::string msg = s_alarmMsg;
    for (char& c : msg)  // aspas/quebra nao tem lugar num string JSON
        if (c == '"' || c == '\\' || c == '\n') c = ' ';
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"armed\":true,\"hour\":%d,\"minute\":%d,\"msg\":\"%s\"}",
             s_alarmH, s_alarmM, msg.c_str());
    return std::string(buf);
}

void TimeManager::tick(bool networkUp) {
    // Rede no ar e hora ainda invalida: insiste a cada 30 s (pacote UDP
    // perdido, DNS lento no boot...) — o intervalo normal do SNTP e 1 h.
    static uint32_t lastTryMs = 0;
    if (ntpEnabled && networkUp && !isTimeValid()) {
        uint32_t now = millis();
        if (lastTryMs == 0 || now - lastTryMs >= 30000) {
            lastTryMs = now;
            syncNTP();
        }
    }

    // Hora valida -> persiste (barato: 1 escrita NVS a cada 10 min)
    static uint32_t lastSaveMs = 0;
    if (isTimeValid()) {
        uint32_t now2 = millis();
        if (lastSaveMs == 0 || now2 - lastSaveMs >= 600000UL) {
            lastSaveMs = now2;
            saveEpoch();
        }
    }

    // Alarme do dia: relogio local passa de hh:mm -> toast e desarma
    if (s_alarmArmed && isTimeValid()) {
        time_t now3;
        time(&now3);
        struct tm ti;
        localtime_r(&now3, &ti);
        if (ti.tm_hour > s_alarmH || (ti.tm_hour == s_alarmH && ti.tm_min >= s_alarmM)) {
            s_alarmArmed = false;
            std::string msg = s_alarmMsg.empty() ? "Alarme" : s_alarmMsg;
            kui::Navigator::toast("ALARME: " + msg, THEME_WARN, 5000);
        }
    }
}

void TimeManager::setManualTime(int year, int month, int day, int hour, int minute) {
    struct tm t;
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = 0;
    t.tm_isdst = -1;
    
    time_t epoch = mktime(&t);
    
    struct timeval now = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&now, NULL);
}

void TimeManager::setTimezone(const std::string& tzOffset) {
    currentTimezone = tzOffset;
    setenv("TZ", currentTimezone.c_str(), 1);
    tzset();
    savePreferences();
}

void TimeManager::setTimeFormat(bool use24h) {
    use24hFormat = use24h;
    savePreferences();
}

void TimeManager::setNTPEnabled(bool enabled) {
    ntpEnabled = enabled;
    savePreferences();
    if (enabled) syncNTP();
}

std::string TimeManager::getFormattedTime() {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    char buffer[16];
    if (use24hFormat) {
        snprintf(buffer, sizeof(buffer), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    } else {
        int h = timeinfo.tm_hour % 12;
        if (h == 0) h = 12;
        const char* ampm = (timeinfo.tm_hour >= 12) ? "PM" : "AM";
        snprintf(buffer, sizeof(buffer), "%02d:%02d %s", h, timeinfo.tm_min, ampm);
    }
    return std::string(buffer);
}

std::string TimeManager::getFormattedDate() {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    char buffer[32];
    // DD/MM/YYYY format
    snprintf(buffer, sizeof(buffer), "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
    return std::string(buffer);
}

int TimeManager::getYear() {
    time_t now; time(&now);
    struct tm timeinfo; localtime_r(&now, &timeinfo);
    return timeinfo.tm_year + 1900;
}

int TimeManager::getMonth() {
    time_t now; time(&now);
    struct tm timeinfo; localtime_r(&now, &timeinfo);
    return timeinfo.tm_mon + 1;
}

int TimeManager::getDay() {
    time_t now; time(&now);
    struct tm timeinfo; localtime_r(&now, &timeinfo);
    return timeinfo.tm_mday;
}

int TimeManager::getSeconds() {
    time_t now; time(&now);
    struct tm timeinfo; localtime_r(&now, &timeinfo);
    return timeinfo.tm_sec;
}
