#include "TimeManager.h"
#include "../FileSystem/FileSystem.h"
#include "../Utils/StrUtils.h"
#include "../Utils/CelerSettings.h"
#include "../UI/Kui.h"
#include "../Display/Theme.h"
#include "../Boards/Board.h"
#include <esp_sntp.h>
#include "esp_netif_sntp.h"
#include "esp_log.h"

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

// ---- RTC externo da placa (hooks do BoardProfile) ---------------------------
// No watch (PCF85063) a hora precisa sobrevive a reboot sem rede: le no boot
// quando a hora do sistema ainda e invalida e grava de volta apos o NTP (ou
// ajuste manual / troca de fuso). A hora gravada e LOCAL — mktime/localtime_r
// sob o TZ ja carregado fazem a conversao.

static void syncExternalRtc(const char* motivo) {
    const BoardProfile& bp = Board::profile();
    if (!bp.writeRtc) return;
    time_t now;
    time(&now);
    struct tm t;
    localtime_r(&now, &t);
    if (bp.writeRtc(t)) {
        ESP_LOGI("celer.time", "RTC externo gravado (%s)", motivo);
    } else {
        ESP_LOGW("celer.time", "RTC externo nao gravou (%s)", motivo);
    }
}

static void restoreExternalRtc() {
    const BoardProfile& bp = Board::profile();
    if (!bp.readRtc) return;
    struct tm t;
    if (!bp.readRtc(t)) {
        // Normal no 1o boot (bateria do RTC nova/oscilador parado) — a hora
        // chega por NTP/ajuste manual e o tick grava o RTC dali em diante.
        ESP_LOGI("celer.time", "RTC externo sem hora valida (aguardando NTP/ajuste)");
        return;
    }
    time_t e = mktime(&t);
    if (e <= 0) return;
    struct timeval now = { .tv_sec = e, .tv_usec = 0 };
    settimeofday(&now, NULL);
    ESP_LOGI("celer.time", "hora restaurada do RTC externo: %04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}

void TimeManager::init() {
    loadPreferences();
    setenv("TZ", currentTimezone.c_str(), 1);
    tzset();
    // RTC externo primeiro (precisao de segundos); o epoch-NVS abaixo e a
    // rede de seguridad das placas sem RTC (drift de minutos).
    restoreExternalRtc();
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
static time_t s_alarmAt = 0;  // epoch do disparo; 0 = recalcular (hora invalida/fuso novo)

// Proxima ocorrencia local de hh:mm estritamente depois de "agora". O teste
// antigo (hora atual > hh:mm) disparava NA HORA um alarme de 08:00 ligado
// as 22:00. mktime normaliza dia 32/DST.
static time_t nextOccurrence(int hour, int minute) {
    time_t now;
    time(&now);
    struct tm t;
    localtime_r(&now, &t);
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = 0;
    t.tm_isdst = -1;
    time_t at = mktime(&t);
    if (at <= now) {
        t.tm_mday += 1;
        t.tm_hour = hour;
        t.tm_min = minute;
        t.tm_sec = 0;
        t.tm_isdst = -1;
        at = mktime(&t);
    }
    return at;
}

bool TimeManager::setAlarm(int hour, int minute, const std::string& msg) {
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
    s_alarmArmed = true;
    s_alarmH = hour;
    s_alarmM = minute;
    s_alarmMsg = msg.substr(0, 64);
    s_alarmAt = isTimeValid() ? nextOccurrence(hour, minute) : 0;
    return true;
}

void TimeManager::clearAlarm() {
    s_alarmArmed = false;
    s_alarmAt = 0;
    s_alarmMsg.clear();
}

bool TimeManager::getAlarm(int& hour, int& minute, std::string& msg) {
    if (!s_alarmArmed) return false;
    hour = s_alarmH;
    minute = s_alarmM;
    msg = s_alarmMsg;
    return true;
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
        // Primeira hora valida da sessao (SNTP seta async): grava no RTC
        // externo uma unica vez — o oscilador dele corre sozinho daqui.
        static bool rtcWritten = false;
        if (!rtcWritten) {
            rtcWritten = true;
            syncExternalRtc("hora valida");
        }
    }

    // Alarme do dia: relogio local passa de hh:mm -> toast e desarma
    if (s_alarmArmed && isTimeValid()) {
        // hora ficou valida depois do setAlarm (NTP) ou o relogio mudou
        if (s_alarmAt == 0) s_alarmAt = nextOccurrence(s_alarmH, s_alarmM);
        time_t now3;
        time(&now3);
        if (now3 >= s_alarmAt) {
            s_alarmArmed = false;
            s_alarmAt = 0;
            std::string msg = s_alarmMsg.empty() ? "Alarme" : s_alarmMsg;
            kui::Navigator::toast("ALARME: " + msg, THEME_WARN, 5000);
        }
    }
}

bool TimeManager::setManualTime(int year, int month, int day, int hour, int minute) {
    if (year < 2020 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return false;
    }
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
    syncExternalRtc("ajuste manual");
    s_alarmAt = 0;  // "proxima hh:mm" relativa a hora nova
    return true;
}

bool TimeManager::setTimezone(const std::string& tzOffset) {
    if (tzOffset.empty() || tzOffset.size() > 48) return false;
    for (char c : tzOffset) {
        if (c == '|' || (unsigned char)c < 0x20 || (unsigned char)c > 0x7E) return false;
    }
    currentTimezone = tzOffset;
    setenv("TZ", currentTimezone.c_str(), 1);
    tzset();
    savePreferences();
    // A hora gravada no RTC e local: fuso novo = hora local nova.
    if (isTimeValid()) syncExternalRtc("fuso trocado");
    s_alarmAt = 0;  // hh:mm e local: o epoch do disparo muda com o fuso
    return true;
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

int TimeManager::getWeekday() {
    time_t now; time(&now);
    struct tm timeinfo; localtime_r(&now, &timeinfo);
    return timeinfo.tm_wday;   // 0=domingo (mesma convencao do RTC/watchface)
}
