#include "Services.h"

#include <Arduino.h>

#include "../../UI/Kui.h"
#include "../../WebManager/WebManager.h"
#include "../../Kernel/TimeManager.h"
#include "../../Kernel/Alarms.h"
#include "../../Display/Backlight.h"
#include "../../Display/ScreenCapture.h"
#include "../../Display/ScreenPower.h"
#include "../../Hardware/Buttons.h"
#include "../../Hardware/PowerPolicy.h"
#include "../../Launcher/LauncherUI.h"
#include "../../Launcher/AlarmScreen.h"
#include "../../Launcher/WatchPanels.h"
#include "../../Launcher/NotificationAlert.h"
#include "../../USBDevice/LogPersist.h"
#include "../../USBDevice/DebugBridge.h"
#if CONFIG_CELEROS_BLUETOOTH
#include "../../Bluetooth/CelerNet.h"
#endif
#if CONFIG_CELEROS_PHONE_LINK
#include "../../Bluetooth/PhoneLink.h"
#include "../../Launcher/PhoneScreens.h"
#endif

// main.cpp: confirmacao do slot OTA apos 30s de boot sao
void confirmPendingOta();

namespace {

void svcNavigator(bool) { kui::Navigator::tick(); }
void svcWebManager(bool) { WebManager::tick(); }
void svcTimeManager(bool) { TimeManager::tick(WebManager::isActive()); }
void svcBacklight(bool) { Backlight::tick(); }
void svcScreenCapture(bool) { ScreenCapture::service(); }
void svcButtons(bool inApp) { Buttons::tick(inApp); }
void svcScreenPower(bool inApp) { ScreenPower::tick(inApp); }
void svcIdleHome(bool) { LauncherUI::idleHomeTick(); }
void svcAlarmScreen(bool) { AlarmScreen::service(); }
void svcWatchPanels(bool) { WatchPanels::service(); }
void svcPowerPolicy(bool) { PowerPolicy::tick(); }
#if CONFIG_CELEROS_PHONE_LINK
void svcPhoneLink(bool inApp) { PhoneLink::tick(inApp); }
void svcPhoneScreens(bool inApp) { PhoneScreens::service(inApp); }
#endif
#if CONFIG_CELEROS_BLUETOOTH
// Malha CelerNet: infraestrutura (sobrevive a troca de app) — scanner,
// bursts de repeticao e presenca; o JS drena com CelerNet.poll().
void svcCelerNet(bool) { CelerNet::tick(); }
#endif
void svcNotificationAlert(bool inApp) { NotificationAlert::service(inApp); }
void svcOtaConfirm(bool) { confirmPendingOta(); }
// LogPersist (kern.log/apps.log): flush a cada ~3 s — ALWAYS para roda com
// app aberto (o teste de voz do cao acontece DENTRO do Dog Face)
void svcLogPersist(bool) { LogPersist::tick(); }
// ALWAYS: com app aberto o bridge segue vivo (logcat/debug de um app em run)
void svcDebugBridge(bool inApp) { DebugBridge::tick(inApp); }

// Alarmes/timer (Kernel/Alarms) conferidos 1x/s ANTES do corte do AOD: com a
// tela apagada/AOD (o normal no watchface) o alarme toca do mesmo jeito. O
// toque e a AlarmScreen nativa: o app sai pelo caminho limpo do X e o
// launcher a empilha (AlarmScreen::service).
void svcAlarmsCheck(bool) {
    static uint32_t s_alarmCheckAt = 0;
    const uint32_t nowA = millis();
    if (nowA - s_alarmCheckAt >= 1000) {
        s_alarmCheckAt = nowA;
        if (Alarms::tick()) LauncherUI::requestAppExit();
    }
}

// A lista e a arquitetura: UM lugar, OS DOIS pumps (celerLoop e present()).
// A ordem preserva a relativa de cada contexto. Diferenca consciente vs o
// celerLoop antigo: NotificationAlert roda DEPOIS de PowerPolicy/PhoneLink
// no contexto LOOP (antes era antes do PowerPolicy) — o pedido de
// acordar/abrir a tela e visto pelo PowerPolicy uma volta depois (~ms).
const CelerServices::Service kServices[] = {
    {"navigator", svcNavigator, CelerServices::LOOP},
    {"webmanager", svcWebManager, CelerServices::LOOP},
    {"timemanager", svcTimeManager, CelerServices::LOOP},
    {"backlight", svcBacklight, CelerServices::ALWAYS},
    // present() chama o SEU ScreenCapture depois do corte do AOD
    // (suppressAppFrame): captura durante AOD com app aberto continua
    // sendo servida so quando o vidro acorda — comportamento preservado.
    {"screencapture", svcScreenCapture, CelerServices::LOOP},
    {"buttons", svcButtons, CelerServices::ALWAYS},
    {"screenpower", svcScreenPower, CelerServices::ALWAYS},
    {"idlehome", svcIdleHome, CelerServices::LOOP},
    {"alarmscreen", svcAlarmScreen, CelerServices::LOOP},
    {"watchpanels", svcWatchPanels, CelerServices::LOOP},
    {"powerpolicy", svcPowerPolicy, CelerServices::ALWAYS},
#if CONFIG_CELEROS_PHONE_LINK
    {"phonelink", svcPhoneLink, CelerServices::ALWAYS},
    {"phonescreens", svcPhoneScreens, CelerServices::LOOP},
#endif
#if CONFIG_CELEROS_BLUETOOTH
    {"celernet", svcCelerNet, CelerServices::ALWAYS},
#endif
    {"notificationalert", svcNotificationAlert, CelerServices::ALWAYS},
    {"otaconfirm", svcOtaConfirm, CelerServices::ALWAYS},
    {"logpersist", svcLogPersist, CelerServices::ALWAYS},
    {"dbgbridge", svcDebugBridge, CelerServices::ALWAYS},
    // fim da fila no present(): erro no callback de alarme nao pula o resto
    {"alarmscheck", svcAlarmsCheck, CelerServices::PRESENT},
};
constexpr int kServiceCount = (int)(sizeof(kServices) / sizeof(kServices[0]));

}  // namespace

void CelerServices::tickLoop() {
    for (int i = 0; i < kServiceCount; i++) {
        if (kServices[i].ctx & LOOP) kServices[i].tick(false);
    }
}

void CelerServices::tickPresent() {
    for (int i = 0; i < kServiceCount; i++) {
        if (kServices[i].ctx & PRESENT) kServices[i].tick(true);
    }
}
