#include "PowerPolicy.h"
#include "BoardIO.h"
#include "../Boards/Board.h"
#include "../Display/ScreenPower.h"
#include "../Utils/CelerSettings.h"
#include "../WebManager/WebManager.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <Arduino.h>
#include <stdlib.h>
#include <string>
#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

namespace {

bool s_on = false;
uint32_t s_checkAt = 0;
uint32_t s_offSince = 0;      // tela apagada desde (0 = acesa)
bool s_wifiAutoOff = false;   // nos desligamos (religa ao acender)
int s_wifiSleepMin = 0;
#if CONFIG_PM_ENABLE
esp_pm_lock_handle_t s_cpuLock = nullptr;   // CPU_FREQ_MAX com a tela acesa
esp_pm_lock_handle_t s_litLock = nullptr;   // NO_LIGHT_SLEEP com a tela acesa (UI sem latencia de wake)
esp_pm_lock_handle_t s_usbLock = nullptr;   // NO_LIGHT_SLEEP com USB presente
bool s_cpuHeld = false;
bool s_litHeld = false;
bool s_usbHeld = false;

void hold(esp_pm_lock_handle_t l, bool& held, bool want) {
    if (l == nullptr || held == want) return;
    held = want;
    if (want) {
        esp_pm_lock_acquire(l);
    } else {
        esp_pm_lock_release(l);
    }
}
#endif

}  // namespace

namespace PowerPolicy {

void init() {
    const BoardProfile& bp = Board::profile();
    if (!bp.autoLightSleep) return;
#if CONFIG_PM_ENABLE
    esp_pm_config_t cfg = {};
    cfg.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    cfg.min_freq_mhz = 40;
    cfg.light_sleep_enable = true;
    if (esp_pm_configure(&cfg) != ESP_OK) {
        ESP_LOGW("celer.power", "esp_pm_configure falhou: sem light sleep");
        return;
    }
    esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "celer.ui", &s_cpuLock);
    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "celer.lit", &s_litLock);
    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "celer.usb", &s_usbLock);
    hold(s_cpuLock, s_cpuHeld, true);  // boot com a tela acesa
    hold(s_litLock, s_litHeld, true);
    hold(s_usbLock, s_usbHeld, true);  // ate o 1o tick ler o VBUS
    s_on = true;
    ESP_LOGI("celer.power", "PM ativo: DFS 40..%d MHz + light sleep com a tela apagada",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
#endif
    reloadSettings();
}

void reloadSettings() {
    std::string v = CelerSettings::get("wifi_sleep_min", "");
    s_wifiSleepMin = v.empty() ? Board::profile().wifiSleepMin : atoi(v.c_str());
}

void tick() {
    if (!s_on) return;
    const uint32_t now = millis();
    if (now - s_checkAt < 1000) return;
    s_checkAt = now;
    const int st = ScreenPower::state();
    const bool lit = st >= 2;
#if CONFIG_PM_ENABLE
    hold(s_cpuLock, s_cpuHeld, lit);
    hold(s_litLock, s_litHeld, lit);
    const int chg = BoardIO::chargeState();
    hold(s_usbLock, s_usbHeld, chg > 0 && (chg & 2));
#endif
    // WiFi ocioso com a tela apagada
    if (lit) {
        s_offSince = 0;
        if (s_wifiAutoOff) {
            s_wifiAutoOff = false;
            if (WebManager::wifiEnabled()) WebManager::enableAsync();
            ESP_LOGI("celer.power", "tela acesa: WiFi religado");
        }
    } else if (st == 0) {
        if (s_offSince == 0) s_offSince = now;
        if (s_wifiSleepMin > 0 && !s_wifiAutoOff && WebManager::isActive() &&
            now - s_offSince >= (uint32_t)s_wifiSleepMin * 60000UL) {
            s_wifiAutoOff = true;
            WebManager::disable();  // nao persiste: a flag nowifi segue intacta
            ESP_LOGI("celer.power", "tela apagada ha %d min: WiFi desligado", s_wifiSleepMin);
        }
    }
}

int loopDelayMs() {
    if (!s_on) return 5;
    return ScreenPower::state() >= 2 ? 5 : 50;
}

}  // namespace PowerPolicy
