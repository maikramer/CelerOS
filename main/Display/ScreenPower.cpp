#include "ScreenPower.h"
#include "Backlight.h"
#include "../Boards/Board.h"
#include "../Hardware/BoardIO.h"
#include "../Kernel/TimeManager.h"
#include "../Utils/CelerSettings.h"
#include "esp_log.h"
#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <time.h>
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "esp_wifi.h"

// packageName do app JS corrente (JSBindings.cpp; via JsInternal.h para o
// runtime — aqui declaramos direto para nao puxar o Duktape no include).
extern std::string s_appPkg;

namespace {

constexpr uint8_t kBrightDim = 0x40;  // ~25% (valor do firmware Rust)
constexpr uint8_t kBrightAod = 0x18;  // brilho do AOD/glance (Rust)
constexpr uint32_t kDimAfterMs = 8000;
constexpr uint32_t kAodAfterMs = 15000;
constexpr uint32_t kBattPollMs = 60000;

CelerDisplay* s_tft = nullptr;
bool s_active = false;
int s_state = 3;          // 3 pleno, 2 dim, 1 AOD, 0 off
bool s_glance = false;    // AOD vindo de raise (expira para off)
uint32_t s_glanceAt = 0;
bool s_blWasOff = false;  // edge do Backlight::isOff()
int s_lastAodMin = -1;
bool s_keepAwake = false;
uint32_t s_glanceMs = 5000;   // 3/5/8 s
bool s_raiseWake = true;

bool watchfaceApp() {
    const char* home = Board::profile().homeApp;
    return home != nullptr && s_appPkg == home;
}

// AOD 1x por minuto com deslocamento anti burn-in (porte do render_aod do
// Rust: shift x = min%9-4, y = (min/9)%9-4 percorre a grade 9x9 em ~81 min).
void drawAod() {
    if (s_tft == nullptr) return;
    time_t now;
    time(&now);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_min == s_lastAodMin) return;
    s_lastAodMin = t.tm_min;

    int shiftX = (t.tm_min % 9) - 4;
    int shiftY = ((t.tm_min / 9) % 9) - 4;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);

    s_tft->fillScreen(0x0000);
    s_tft->setTextColor(0x7BEF);  // cinza medio (metade do branco)
    int w = s_tft->textWidth(buf, 6);
    s_tft->drawString(buf, s_tft->width() / 2 - w / 2 + shiftX,
                      s_tft->height() / 2 - 40 + shiftY, 6);

    int mv = BoardIO::batteryMv();
    if (mv >= 3000 && mv <= 5000) {
        char b[10];
        snprintf(b, sizeof(b), "%d,%dV", mv / 1000, (mv % 1000) / 100);
        s_tft->setTextColor(0x39E7);  // cinza escuro
        int bw = s_tft->textWidth(b, 2);
        s_tft->drawString(b, s_tft->width() / 2 - bw / 2 + shiftX,
                          s_tft->height() / 2 + 50 + shiftY, 2);
    }
}

void enterGlance(uint32_t now) {
    const BoardProfile& bp = Board::profile();
    if (s_state == 0 && bp.screenWake) bp.screenWake();  // painel estava em SLPIN
    Backlight::dim(kBrightAod);
    s_glance = true;
    s_glanceAt = now;
    s_state = 1;
    s_lastAodMin = -1;  // força o desenho
    drawAod();
    ESP_LOGI("celer.screen", "glance (raise)");
}

void sleepNow() {
    const BoardProfile& bp = Board::profile();
    Backlight::forceOff();
    if (bp.screenSleep) bp.screenSleep();
    s_state = 0;
    s_glance = false;
    ESP_LOGI("celer.screen", "tela off (painel em SLPIN)");
}

}  // namespace

namespace ScreenPower {

void init() {
    const BoardProfile& bp = Board::profile();
    if (bp.screenSleep == nullptr) return;  // placa sem estados de tela
    s_tft = &Board::display();
    s_active = true;

    int offMin = atoi(CelerSettings::get("screen_off_min", "3").c_str());
    if (offMin < 1) offMin = 1;
    if (offMin > 5) offMin = 5;
    int glance = atoi(CelerSettings::get("glance_sec", "5").c_str());
    if (glance != 3 && glance != 8) glance = 5;
    s_glanceMs = glance * 1000UL;
    s_raiseWake = CelerSettings::get("raise_wake", "1") == "1";

    // OFF logico continua no Backlight (timeout + wake consumido no touch)
    Backlight::setIdleTimeout((uint32_t)offMin * 60000UL, false);
    ESP_LOGI("celer.screen", "estados de tela ativos (off %d min, glance %d s, raise %d)",
             offMin, glance, (int)s_raiseWake);
}

void tick(bool inApp) {
    if (!s_active) return;
    const BoardProfile& bp = Board::profile();
    uint32_t now = millis();
    uint32_t idle = now - Backlight::lastActivity();

    // Edges do Backlight (OFF logico): dormir/acordar o painel de verdade
    bool blOff = Backlight::isOff();
    if (blOff && !s_blWasOff) {
        s_blWasOff = true;
        if (!s_glance && s_state != 0) {
            if (bp.screenSleep) bp.screenSleep();
            s_state = 0;
            ESP_LOGI("celer.screen", "tela off (painel em SLPIN)");
        }
    } else if (!blOff && s_blWasOff) {
        s_blWasOff = false;
        if (s_state <= 1) {
            // acordou por toque/config: painel em SLPIN engoliu o brilho que
            // o noteActivity re-aplicou — acorda e re-aplica de verdade
            if (bp.screenWake) bp.screenWake();
            Backlight::undim();
            s_state = 3;
            s_glance = false;
            ESP_LOGI("celer.screen", "tela acordou");
        }
    }

    // Raise -> glance (so com tela apagada/AOD, como no firmware Rust)
    if (s_raiseWake && bp.raisePoll && bp.raisePoll()) {
        if (Backlight::isOff() || s_state <= 1) enterGlance(now);
    }

    switch (s_state) {
        case 3:
            if (s_keepAwake) break;  // jogo segurando a tela
            if (idle >= kDimAfterMs) {
                Backlight::dim(kBrightDim);
                s_state = 2;
            }
            break;
        case 2:
            if (s_keepAwake || idle < kDimAfterMs) {
                Backlight::undim();
                s_state = 3;
            } else if (idle >= kAodAfterMs && inApp && watchfaceApp()) {
                // AOD so sobre o Watchface (ele redesenha a tela inteira por
                // segundo; qualquer outro app segue dim ate o timeout)
                Backlight::dim(kBrightAod);
                s_state = 1;
                s_glance = false;
                s_lastAodMin = -1;
                drawAod();
                ESP_LOGI("celer.screen", "AOD");
            }
            break;
        case 1:
            if (idle < kDimAfterMs) {
                // atividade com AOD normal: acende tudo
                Backlight::undim();
                s_state = 3;
                s_glance = false;
                break;
            }
            if (s_glance && now - s_glanceAt >= s_glanceMs) {
                sleepNow();  // glance expirou: dorme de verdade
                break;
            }
            drawAod();  // no-op ate o minuto virar
            break;
        default:
            break;
    }
}

bool suppressAppFrame() { return s_active && s_state <= 1; }

void keepAwake(bool on) { s_keepAwake = on && s_active; }

void deepSleepNow() {
    const BoardProfile& bp = Board::profile();
    ESP_LOGI("celer.power", "deep sleep (EXT1 nos botoes)");

    // Painel: dorme mesmo se a tela ainda estava acesa (brilho 0 + SLPIN)
    if (bp.screenSleep && s_state != 0) bp.screenSleep();

    // Ritual da placa: passos no NVS, IMU fora, perifericos da board
    if (bp.sleepPrep) bp.sleepPrep();

    // Radio fora (desconecta limpo; ao acordar e reboot, tudo re-sobe)
    esp_wifi_stop();

    // Wake: EXT1 nivel BAIXO nos botoes do perfil (BOOT/PWR do watch)
    uint64_t mask = 0;
    int pins[2] = {bp.buttonPin, bp.buttonPin2};
    for (int p : pins) {
        if (p < 0) continue;
        if (rtc_gpio_is_valid_gpio((gpio_num_t)p)) {
            rtc_gpio_init((gpio_num_t)p);
            rtc_gpio_set_direction((gpio_num_t)p, RTC_GPIO_MODE_INPUT_ONLY);
            rtc_gpio_pulldown_dis((gpio_num_t)p);
            rtc_gpio_pullup_en((gpio_num_t)p);
            mask |= 1ULL << p;
        }
    }
    if (mask != 0) {
        // ESP32 classico so tem ALL_LOW/ANY_HIGH no EXT1 (sem ANY_LOW, que e
        // do S2/S3/C3+). Caminho morto nele hoje (placas com botao = S3), mas
        // o firmware compartilhado tem que compilar em todos os alvos.
#if CONFIG_IDF_TARGET_ESP32
        esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ALL_LOW);
#else
        esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
    }

    // RTC externo segura a hora (PCF85063 no proprio oscilador) — nada a fazer
    esp_deep_sleep_start();
}

}  // namespace ScreenPower
