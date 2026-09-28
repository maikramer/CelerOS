#include "TouchCalibrator.h"
#include "../FileSystem/FileSystem.h"
#include "esp_system.h"

extern int currentState;

CelerDisplay *TouchCalibrator::tftInstance = nullptr;

void TouchCalibrator::init(CelerDisplay *tft) {
    tftInstance = tft;
}

void TouchCalibrator::runCalibration() {
    if (!tftInstance) return;

    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->setTextDatum(TC_DATUM);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString("Touch Calibration", 120, 50, 4);
    tftInstance->drawString("Touch the arrows at the corners", 120, 100, 2);

    uint16_t calData[5];
    tftInstance->calibrateTouch(calData, TFT_RED, TFT_BLACK, 15);

    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawString("Calibration Saved!", 120, 120, 2);

    if (calData[4]) {
        // calibrador rejeitou as amostras: nada a salvar — reinicia e o
        // calibrador roda de novo (sem arquivo valido no LittleFS)
        celer_log_println("calibracao rejeitada — reiniciando para nova tentativa");
        delay(400);
        ESP.restart();
    }
    FileSystem::writeCalData(calData);
    tftInstance->setTouch(calData);

    delay(1000);
    // Transition back to Launcher
    currentState = 0; 
}
