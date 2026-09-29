#ifndef CELER_KERNEL_H
#define CELER_KERNEL_H

#include <Arduino.h>
#include <string>
#include "../../Boards/Board.h"
#include <duktape.h>

class CelerKernel {
public:
    static void init(CelerDisplay *tft);
    static void runFile(const char* filePath, const char* appTitle = "", bool topbarFixed = true,
                        const char* appPkg = "", uint32_t perms = 0xFFFFFFFFu);
    static void loop();
    static void executeJS(const char* jsCode);
    static std::string checkSyntax(const char* jsCode);
    static duk_context *ctx;
    static CelerDisplay *tftInstance;
    // Heap livre para o app corrente no inicio (DRAM + IRAM de transbordo,
    // apos liberar os caches do launcher, antes do fonte/heap JS):
    // System.getInfo().appRAM
    static size_t appLaunchFreeHeap;
    // Mensagem do fatal do Duktape que reiniciou o aparelho (uma vez; nullptr
    // se o ultimo reset nao foi um fatal JS)
    static const char* takeLastFatal();

private:
    static void checkJSError(duk_context *ctx, duk_int_t result);
};

#endif // CELER_KERNEL_H
