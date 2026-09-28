#ifndef CELER_KERNEL_H
#define CELER_KERNEL_H

#include <Arduino.h>
#include <string>
#include "../../Boards/Board.h"
#include <duktape.h>

class CelerKernel {
public:
    static void init(CelerDisplay *tft);
    static void runFile(const char* filePath, const char* appTitle = "", bool topbarFixed = true);
    static void loop();
    static void executeJS(const char* jsCode);
    static std::string checkSyntax(const char* jsCode);
    static duk_context *ctx;
    static CelerDisplay *tftInstance;

private:
    static void checkJSError(duk_context *ctx, duk_int_t result);
};

#endif // CELER_KERNEL_H
