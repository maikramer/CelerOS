#include <Arduino.h>
#include "USBDevice/LogSink.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_sleep.h"
#include "esp_ota_ops.h"
#include "esp_core_dump.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Boards/Board.h"
#include "Display/Layout.h"
#include "Display/Theme.h"
#include "Display/Backlight.h"
#include "Display/ScreenCapture.h"
#include "Display/ScreenPower.h"
#include "Hardware/Buttons.h"
#include "FileSystem/FileSystem.h"
#include "Launcher/LauncherUI.h"
#include "Launcher/AlarmScreen.h"
#include "Launcher/WatchPanels.h"
#include "Launcher/NotificationAlert.h"
#include "Hardware/PowerPolicy.h"
#if CONFIG_CELEROS_PHONE_LINK
#include "Bluetooth/PhoneLink.h"
#include "Launcher/PhoneScreens.h"
#endif
#include "Kernel/Alarms.h"
#include "Kernel/Services.h"
#include "Settings/TouchCalibrator.h"
#include "WebManager/WebManager.h"
#include "WebManager/WifiSetupPortal.h"
#include "Runtime/JSBindings.h"
#include "Assets/SplashLogo.h"
#include "Kernel/Core/CelerKernel.h"
#include "Kernel/TimeManager.h"
#include "Launcher/Screens.h"
#include "USBDevice/SerialLink.h"
#include "Utils/I18n.h"
#include "Boards/Board.h"
#if CONFIG_CELEROS_USB_NATIVE
#include "USBDevice/USBDevice.h"
#endif

// Telas de sistema (Settings, App Store, Installer, Help, Web Server) sao
// apps JS em /local/apps (W8) — o firmware carrega so o core + launcher.
// currentState sobrevive apenas para o TouchCalibrator sinalizar "voltar
// ao launcher" no boot.
#define STATE_LAUNCHER 0

int currentState = STATE_LAUNCHER;
CelerDisplay& tft = Board::display();
static LauncherScreen s_launcher;  // base da pilha do Navigator

// Splash de boot no tema: logo embutida (gerada por tools/make_splash.py) +
// status + trilho da barra de progresso (a barra e preenchida pelo scan de
// apps em (sx(20), sy(200), sx(200), sy(10))). O blit usa zoom do design
// (ex.: 2x no 480x480), entao o asset fica so na resolucao 240.
static void bootSplash(const char* status) {
    if (Board::profile().headless) {
        (void)status;  // sem vidro: painel stub descartaria tudo — nao gasta o boot
        return;
    }
    tft.fillScreen(THEME_BG);
    const float zoom = (float)UI::W / 240.0f;
    tft.drawPng(kSplashLogoPng, sizeof(kSplashLogoPng),
                UI::cx() - (int32_t)(SPLASH_LOGO_W * zoom / 2.0f),
                UI::sy(115) - (int32_t)(SPLASH_LOGO_H * zoom / 2.0f),
                0, 0, 0, 0, zoom, zoom);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(THEME_TEXT_DIM);
    tft.drawString(status, UI::cx(), UI::sy(175), UI::font(1));
    tft.fillRoundRect(UI::sx(20), UI::sy(200), UI::sx(200), UI::sy(10), UI::sy(5), THEME_CARD);
}

static void celerSetup() {
    Serial.begin(115200);
    celer_log_println("\n--- CelerOS Booting ---");
    celer_log_printf("board: %s\n", Board::profile().name);
    if (Board::profile().hasPsram) {
        celer_log_printf("PSRAM: %u bytes (free %u)\n", (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());
    }

    // Init TFT (HAL da placa)
    Board::init();
    ScreenCapture::init();  // esta task e a dona do display (capturas passam por ela)
    tft.setRotation(Board::profile().rotation);
    UI::init(tft.width(), tft.height(), Board::profile().screenInset);
    ESP_LOGI("celer.lcd", "depth=%d rot=%d w=%d h=%d",
             (int)tft.getColorDepth(), (int)tft.getRotation(), tft.width(), tft.height());


    bootSplash(i18n::TR("Iniciando...", "Starting..."));

    // Initialize File Systems (LittleFS & SD)
    if (!FileSystem::init()) {
        celer_log_println("File System Warning: One or more FS failed to mount.");
        tft.setTextColor(THEME_WARN);
        tft.drawString("Aviso: falha ao montar FS", UI::cx(), UI::sy(230), UI::font(1));
        delay(1000);
    }

#if !CONFIG_CELEROS_USB_NATIVE
    // Console/shell + canal celerctl na UART do console (CH340 no PC). Com
    // USB nativo (watch) o celerctl vai pelo CDC e a UART0 nao tem conector:
    // a task + o HostLink dela so comeriam RAM interna.
    SerialLink::init();
#else
    SerialLink::initLogOnly();  // logcat continua recebendo os ESP_LOG*
#endif
#if CONFIG_CELEROS_USB_NATIVE
    // USB nativo (TinyUSB): so para placas com GPIO19/20 livres
    USBDevice::init();
#endif

    // Brilho do backlight (depois do FS: le /local/brightness.txt)
    Backlight::init(&tft);

    // Botoes fisicos do perfil (no-op sem pins; watch: BOOT/PWR)
    Buttons::init();

    // Estados de tela do watch (dim/AOD/off; no-op sem hooks no perfil)
    ScreenPower::init();
    PowerPolicy::init();  // DFS + light sleep guiados pela tela (watch)
    
    // Initialize Time Manager
    TimeManager::init();
    Alarms::init();  // alarmes/timer/soneca do NVS (API 15)
    
    celer_log_printf("DEBUG: Free heap before Kernel: %u\n", (unsigned)ESP.getFreeHeap());

    // Initialize JS Runtime
    celer_log_println("DEBUG: Starting CelerKernel...");
    CelerKernel::init(&tft);
    celer_log_println("CelerKernel initialized successfully.");
    celer_log_printf("DEBUG: Free heap after Kernel: %u\n", (unsigned)ESP.getFreeHeap());

    // Init UI Components
    LauncherUI::init(&tft);
    TouchCalibrator::init(&tft);

    // Initial App Scan (barra da splash: LauncherUI::scanLocalApps preenche)
    celer_log_println("DEBUG: Scanning Local Apps...");
    bootSplash(i18n::TR("Carregando apps...", "Loading apps..."));
    LauncherUI::scanLocalApps();
    LauncherUI::applyAutostart();  // /local/autostart.txt (ex.: cara do cao)
    LauncherUI::needsRescan = false;
    celer_log_println("DEBUG: Local Apps Scanned.");

    // WiFi 100% assincrono: prepara credenciais/eventos e a task de
    // reconexao do NetworkManager conecta sozinha (sem race de scan —
    // o boot antigo concorria com a task e caia na tela "WiFi Setup").
    // Sem rede, o launcher mostra o banner "WiFi offline". Sobe DEPOIS do
    // scan de apps: sem PSRAM, o prewarm dos icones (decode PNG, ~44KB
    // transitorios) precisa do heap cheio.
    WebManager::startAsync();

    // Touch resistivo sem calibracao salva: roda o calibrador antes da UI.
    // Placas com touch capacitivo (GT911) pulam a calibracao.
    if (!Board::profile().capacitiveTouch) {
        uint16_t calData[5];
        if (!FileSystem::readCalData(calData)) {
            celer_log_println("No calibration data. Entering calibrator.");
            TouchCalibrator::runCalibration();
        } else {
            tft.setTouch(calData);
        }
    }

    // UI nova: launcher e a base da pilha do Navigator
    kui::Navigator::begin(tft);
    WatchPanels::init();  // gestos de borda do relogio (no-op nas outras placas)
#if CONFIG_CELEROS_PHONE_LINK
    PhoneLink::init();    // Gadgetbridge (Bangle.js) no ar se "phone_on"
#endif
    kui::Navigator::push(&s_launcher);
    currentState = STATE_LAUNCHER;

    // Diagnostico pos-boot: reinicio por watchdog de task (app travado) ou
    // coredump gravado na particao dedicada viram toast no launcher
    esp_reset_reason_t rr = esp_reset_reason();
    celer_log_printf("reset: motivo %d\n", (int)rr);  // esp_reset_reason_t
    if (rr == ESP_RST_DEEPSLEEP) {
        // Watch dormindo acordou (EXT1 dos botoes): a causa diz quem foi
        // IDF 6: as causas vem num bitmap (bit = esp_sleep_wakeup_cause_t)
        const uint32_t causes = esp_sleep_get_wakeup_causes();
        if (causes & (1u << ESP_SLEEP_WAKEUP_EXT1)) {
            uint64_t m = esp_sleep_get_ext1_wakeup_status();
            celer_log_printf("acordou do deep sleep: botao (ext1 mask 0x%llx)\n",
                             (unsigned long long)m);
        } else {
            celer_log_printf("acordou do deep sleep: causas 0x%lx\n", (unsigned long)causes);
        }
    }
    if (const char* fatal = CelerKernel::takeLastFatal()) {
        celer_log_printf("reiniciou por fatal do runtime JS: %s\n", fatal);
        kui::Navigator::toast(i18n::TR("Um app ficou sem memória e o sistema reiniciou",
                                       "An app ran out of memory and the system restarted"),
                              THEME_WARN, 4000);
    }
    if (rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT) {
        kui::Navigator::toast(i18n::TR("Um app travou e o sistema reiniciou", "An app froze and the system restarted"), THEME_WARN, 4000);
    }
    {
        size_t cdAddr = 0, cdSize = 0;
        if (esp_core_dump_image_get(&cdAddr, &cdSize) == ESP_OK && cdSize > 0) {
            celer_log_println("coredump presente no flash (celerctl coredump para ler)");
        }
    }
    if (FileSystem::localMountFailed()) {
        kui::Navigator::toast(i18n::TR("Armazenamento interno corrompido: recupere pelo USB",
                                       "Internal storage corrupted: recover over USB"),
                              THEME_WARN, 5000);
    }

    // Watchdog da main task: inscricao DEPOIS da calibracao (calibrateTouch
    // do LovyanGFX espera o usuario sem timeout e e imexivel — submodule).
    // Durante todo o boot os idle tasks ja sao vigiados (PANIC=y).
    esp_task_wdt_add(nullptr);
    celer_log_println("DEBUG: Setup complete, entering loop!");
}

// OTA com rollback: o slot novo boota como PENDING_VERIFY; 30s de uptime sao
// (primeiro tick apos a marca) confirmam com esp_ota_mark_app_valid. Crash ou
// reset antes disso e o bootloader reverte para o slot anterior sozinho.
// Tambem chamado pelo present() dos apps (JSBindings): com o app casa aberto
// desde o boot (watchface) o celerLoop nao roda e o slot ficava pendente para
// sempre — o OTA seguinte era recusado e um reboot desfazia a atualizacao.
void confirmPendingOta() {
    static bool checked = false;
    if (checked || esp_timer_get_time() < 30LL * 1000000LL) return;
    checked = true;
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(running, &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        celer_log_println("OTA confirmada: boot sao por 30s");
    }
}

static void celerLoop() {
    esp_task_wdt_reset();  // coracao do watchdog: UI viva

    // UI (input + redraw)
    // Servicos do OS (WebManager, TimeManager, Backlight, ScreenCapture,
    // Buttons, ScreenPower, launcher, alarmes, paineis, PowerPolicy,
    // PhoneLink, alerta de notificacao, confirmacao de OTA): a lista
    // ordenada vive em Kernel/Services.cpp e e a MESMA que o present()
    // dos apps percorre — um lugar so, dois contextos.
    CelerServices::tickLoop();

    delay(PowerPolicy::loopDelayMs());  // tela apagada: loop lento, CPU dorme
}

// Entry point ESP-IDF: setup + loop na main task (stack 32KB via sdkconfig)
extern "C" void app_main(void) {
    celerSetup();
    while (true) {
        celerLoop();
    }
}
