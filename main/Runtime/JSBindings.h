#ifndef JS_BINDINGS_H
#define JS_BINDINGS_H

#include <Arduino.h>
#include <duktape.h>
#include "../Boards/Board.h"

#include "../Utils/AppPerms.h"  // enum AppPerm/PERM_* (header testavel no host)
using celer::PERM_ALL;

class JSBindings {
public:
    // appTitle: nome na topbar do sistema; topbarFixed: false = faixa
    // retratil (swipe da borda superior revela por alguns segundos) com o
    // app em tela cheia — campo "topbar" do app.json
    static void init(duk_context *ctx, CelerDisplay *tft, const char* appTitle = "",
                    bool topbarFixed = true, const char* appPkg = "",
                    uint32_t perms = PERM_ALL);

    // Alvo de desenho do app (sprite do app > quadro automatico > display)
    static lgfx::LGFXBase* gfx();
    // Leva o quadro automatico ao vidro se houve desenho desde o ultimo
    static void present();
    // Recorte do display quando o app desenha direto nele (sem quadro PSRAM):
    // o do app (System.setClip) intersectado com a area abaixo da topbar
    static void setAppDisplayClip(bool on, int x, int y, int w, int h);
    static void applyDisplayClip();
    // y fisico do desenho no alvo corrente (quadro/display descontam a topbar
    // do sistema; o sprite do app e canvas proprio, origem em 0)
    static int mapY(int v);

private:
    static CelerDisplay *tftInstance;
    static CelerSprite *tftSprite;
    static duk_context *s_jsCtx;  // heap do app corrente (timers no present)

    // Double Buffering
    static duk_ret_t js_createSprite(duk_context *ctx);
    static duk_ret_t js_deleteSprite(duk_context *ctx);
    static duk_ret_t js_pushSprite(duk_context *ctx);
    static duk_ret_t js_bindSprite(duk_context *ctx);
    static duk_ret_t js_useSprite(duk_context *ctx);   // troca de alvo (API 12)
    static void deleteAllSprites();                    // reset por app
    static bool useSprite;

    // Timers JS (API 12): globais setTimeout/setInterval/clear*
    static duk_ret_t js_setTimeout(duk_context *ctx);
    static duk_ret_t js_setInterval(duk_context *ctx);
    static duk_ret_t js_clearTimeout(duk_context *ctx);
    static duk_ret_t js_clearInterval(duk_context *ctx);
    static void timersTick(duk_context *ctx);   // chamado pelo present()
    static void timersResetAll();               // reset por app

    // GPIO Bindings
    static duk_ret_t js_pinMode(duk_context *ctx);
    static duk_ret_t js_digitalWrite(duk_context *ctx);
    static duk_ret_t js_digitalRead(duk_context *ctx);
    static duk_ret_t js_analogRead(duk_context *ctx);
    static duk_ret_t js_analogWrite(duk_context *ctx);
    static duk_ret_t js_pulseIn(duk_context *ctx);
    static duk_ret_t js_servo(duk_context *ctx);    // 50 Hz via LEDC (API 10)
    static duk_ret_t js_servoOff(duk_context *ctx);

    // Display Bindings - Drawing
    static duk_ret_t js_fillScreen(duk_context *ctx);
    static duk_ret_t js_fillRect(duk_context *ctx);
    static duk_ret_t js_drawRect(duk_context *ctx);
    static duk_ret_t js_drawFastVLine(duk_context *ctx);
    static duk_ret_t js_drawFastHLine(duk_context *ctx);
    static duk_ret_t js_drawLine(duk_context *ctx);
    static duk_ret_t js_drawPixel(duk_context *ctx);
    static duk_ret_t js_drawCircle(duk_context *ctx);
    static duk_ret_t js_fillCircle(duk_context *ctx);
    static duk_ret_t js_drawTriangle(duk_context *ctx);
    static duk_ret_t js_fillTriangle(duk_context *ctx);
    static duk_ret_t js_drawRoundRect(duk_context *ctx);
    static duk_ret_t js_fillRoundRect(duk_context *ctx);
    static duk_ret_t js_drawBMP(duk_context *ctx);
    static duk_ret_t js_drawPNG(duk_context *ctx);

    // Display Bindings - Text
    static duk_ret_t js_drawString(duk_context *ctx);
    static duk_ret_t js_setTextColor(duk_context *ctx);
    static duk_ret_t js_setTextSize(duk_context *ctx);
    static duk_ret_t js_setTextDatum(duk_context *ctx);  // API 12

    // Storage (API 12): chave-valor NVS privado por packageName
    static duk_ret_t js_storageGet(duk_context *ctx);
    static duk_ret_t js_storageSet(duk_context *ctx);
    static duk_ret_t js_storageRemove(duk_context *ctx);
    static duk_ret_t js_storageClear(duk_context *ctx);
    static duk_ret_t js_storageClearFor(duk_context *ctx);  // system-gated

    // Display Bindings - Utility
    static duk_ret_t js_color(duk_context *ctx);
    static duk_ret_t js_screenWidth(duk_context *ctx);
    static duk_ret_t js_screenHeight(duk_context *ctx);

    // Touch Input
    static duk_ret_t js_getTouch(duk_context *ctx);

    // System Utilities
    static duk_ret_t js_millis(duk_context *ctx);
    static duk_ret_t js_micros(duk_context *ctx);
    static duk_ret_t js_delay(duk_context *ctx);
    static duk_ret_t js_delayMicroseconds(duk_context *ctx);
    static duk_ret_t js_print(duk_context *ctx);
    static duk_ret_t js_getTemperature(duk_context *ctx);
    static duk_ret_t js_hasTemperatureSensor(duk_context *ctx);
    static duk_ret_t js_getInfo(duk_context *ctx);
    static duk_ret_t js_restart(duk_context *ctx);

    static duk_ret_t js_getTime(duk_context *ctx);
    static duk_ret_t js_getSeconds(duk_context *ctx);
    static duk_ret_t js_getDate(duk_context *ctx);
    static duk_ret_t js_getYear(duk_context *ctx);
    static duk_ret_t js_getMonth(duk_context *ctx);
    static duk_ret_t js_getDay(duk_context *ctx);
    static duk_ret_t js_getTimezone(duk_context *ctx);
    
    static duk_ret_t js_getOSVersion(duk_context *ctx);
    static duk_ret_t js_getAPILevel(duk_context *ctx);

    // Network Bindings
    static duk_ret_t js_getIPAddress(duk_context *ctx);
    static duk_ret_t js_isWiFiActive(duk_context *ctx);

    // Network Bindings - HTTP (objeto Net, API level 2)
    static duk_ret_t js_netGet(duk_context *ctx);
    static duk_ret_t js_netGetJSON(duk_context *ctx);
    static duk_ret_t js_netPost(duk_context *ctx);
    static duk_ret_t js_netDownload(duk_context *ctx);  // streaming p/ arquivo (API 6)
    static duk_ret_t js_netIsConnected(duk_context *ctx);

    // FileSystem Bindings
    static duk_ret_t js_readTextFile(duk_context *ctx);
    static duk_ret_t js_writeTextFile(duk_context *ctx);
    static duk_ret_t js_appendTextFile(duk_context *ctx);
    static duk_ret_t js_readFile(duk_context *ctx);    // binario (API 12)
    static duk_ret_t js_writeFile(duk_context *ctx);   // binario (API 12)
    static duk_ret_t js_deleteFile(duk_context *ctx);
    static duk_ret_t js_renameFile(duk_context *ctx);
    static duk_ret_t js_fileExists(duk_context *ctx);
    static duk_ret_t js_listDir(duk_context *ctx);
    static duk_ret_t js_mkdir(duk_context *ctx);
    static duk_ret_t js_rmdir(duk_context *ctx);
    static duk_ret_t js_isDirectory(duk_context *ctx);
    static duk_ret_t js_isFile(duk_context *ctx);
    static duk_ret_t js_getFileSize(duk_context *ctx);
    static duk_ret_t js_getTotalSpace(duk_context *ctx);
    static duk_ret_t js_getUsedSpace(duk_context *ctx);
    static duk_ret_t js_getFreeSpace(duk_context *ctx);
    static duk_ret_t js_getFileMD5(duk_context *ctx);
    static duk_ret_t js_mountSD(duk_context *ctx);
    static duk_ret_t js_unmountSD(duk_context *ctx);

    // Keyboard Bindings
    static duk_ret_t js_prompt(duk_context *ctx);

    // Keyboard acoplado (System.keypad*, API level 5 — sessao nao-bloqueante)
    static duk_ret_t js_keypadOpen(duk_context *ctx);
    static duk_ret_t js_keypadPoll(duk_context *ctx);
    static duk_ret_t js_keypadText(duk_context *ctx);
    static duk_ret_t js_keypadRect(duk_context *ctx);
    static duk_ret_t js_keypadDraw(duk_context *ctx);
    static duk_ret_t js_keypadClose(duk_context *ctx);

    // Topbar custom (API level 6) — texto e chips da faixa
    static duk_ret_t js_topbarText(duk_context *ctx);
    static duk_ret_t js_topbarButtons(duk_context *ctx);
    static duk_ret_t js_topbarPop(duk_context *ctx);

    // System nivel 3 (apps de sistema em JS — W8)
    static duk_ret_t js_textWidth(duk_context *ctx);
    static duk_ret_t js_fontHeight(duk_context *ctx);
    static duk_ret_t js_theme(duk_context *ctx);
    static duk_ret_t js_drawIcon(duk_context *ctx);
    static duk_ret_t js_copyFile(duk_context *ctx);
    static duk_ret_t js_copyDirectory(duk_context *ctx);
    static duk_ret_t js_removeDirectory(duk_context *ctx);
    static duk_ret_t js_setBrightness(duk_context *ctx);
    static duk_ret_t js_getBrightness(duk_context *ctx);
    static duk_ret_t js_backlightSupported(duk_context *ctx);
    static duk_ret_t js_openWifiSetup(duk_context *ctx);
    static duk_ret_t js_exitApp(duk_context *ctx);
    static duk_ret_t js_present(duk_context *ctx);
    static duk_ret_t js_setClip(duk_context *ctx);
    static duk_ret_t js_clearClip(duk_context *ctx);
    static duk_ret_t js_isBuffered(duk_context *ctx);
    static duk_ret_t js_wifiStatus(duk_context *ctx);
    static duk_ret_t js_md5(duk_context *ctx);
    static duk_ret_t js_setPin(duk_context *ctx);
    static duk_ret_t js_verifyPin(duk_context *ctx);
    static duk_ret_t js_pinClear(duk_context *ctx);
    static duk_ret_t js_pinState(duk_context *ctx);
    static duk_ret_t js_webAuthInfo(duk_context *ctx);
    static duk_ret_t js_webAuthSetPass(duk_context *ctx);
    static duk_ret_t js_setting(duk_context *ctx);
    static duk_ret_t js_toast(duk_context *ctx);
    static duk_ret_t js_beep(duk_context *ctx);
    static duk_ret_t js_led(duk_context *ctx);
    static duk_ret_t js_relay(duk_context *ctx);
    static duk_ret_t js_relayState(duk_context *ctx);
    static duk_ret_t js_relayCount(duk_context *ctx);
    static duk_ret_t js_battery(duk_context *ctx);
    static duk_ret_t js_micLevel(duk_context *ctx);
    static duk_ret_t js_touchPad(duk_context *ctx);
    static duk_ret_t js_neopixel(duk_context *ctx);
    static duk_ret_t js_setAutoBrightness(duk_context *ctx);
    static duk_ret_t js_getAutoBrightness(duk_context *ctx);
    static duk_ret_t js_lightLevel(duk_context *ctx);
    static duk_ret_t js_appData(duk_context *ctx);
    static duk_ret_t js_rescanApps(duk_context *ctx);
    static duk_ret_t js_factoryReset(duk_context *ctx);
    static duk_ret_t js_otaCheck(duk_context *ctx);
    static duk_ret_t js_otaStart(duk_context *ctx);
    static duk_ret_t js_setTimezone(duk_context *ctx);
    static duk_ret_t js_setManualTime(duk_context *ctx);
    static duk_ret_t js_set24hFormat(duk_context *ctx);
    static duk_ret_t js_get24hFormat(duk_context *ctx);
    static duk_ret_t js_setNtpEnabled(duk_context *ctx);
    static duk_ret_t js_getNtpEnabled(duk_context *ctx);
    static duk_ret_t js_webActive(duk_context *ctx);
    static duk_ret_t js_webSetActive(duk_context *ctx);

    // Net assincrono (F3): handle + poll
    static duk_ret_t js_netBeginGet(duk_context *ctx);
    static duk_ret_t js_netPollGet(duk_context *ctx);
    static duk_ret_t js_netCancelGet(duk_context *ctx);
    // Descarta zumbis/resultados do app anterior (chamado no init de cada app)
    static void netAsyncReset();

    // Net nivel 3 (WiFi)
    static duk_ret_t js_wifiScan(duk_context *ctx);
    static duk_ret_t js_wifiConnect(duk_context *ctx);
    static duk_ret_t js_wifiDisconnect(duk_context *ctx);

    // Celer Link Bluetooth (objeto global CelerLink, API 9 — requer
    // CONFIG_CELEROS_BLUETOOTH; desligado, o objeto nao existe).
    // Pareamento por codigo: API 11 (verify/unpair, start com opts).
    static duk_ret_t js_linkStart(duk_context *ctx);
    static duk_ret_t js_linkStop(duk_context *ctx);
    static duk_ret_t js_linkScan(duk_context *ctx);
    static duk_ret_t js_linkConnect(duk_context *ctx);
    static duk_ret_t js_linkDisconnect(duk_context *ctx);
    static duk_ret_t js_linkSend(duk_context *ctx);
    static duk_ret_t js_linkPoll(duk_context *ctx);
    static duk_ret_t js_linkStatus(duk_context *ctx);
    static duk_ret_t js_linkVerify(duk_context *ctx);
    static duk_ret_t js_linkUnpair(duk_context *ctx);

    // Helper
    static void fatalErrorHandler(void *udata, const char *msg);
};

#endif // JS_BINDINGS_H
