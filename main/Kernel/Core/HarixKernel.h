#ifndef HARIX_KERNEL_H
#define HARIX_KERNEL_H

#include <Arduino.h>
#include <string>
#include "../../Boards/Board.h"
#include <duktape.h>

class HarixKernel {
public:
    static void init(KryonDisplay *tft);
    static void runFile(const char* filePath);
    static void loop();
    static void executeJS(const char* jsCode);
    static std::string checkSyntax(const char* jsCode);
    static duk_context *ctx;
    static KryonDisplay *tftInstance;

private:
    static void checkJSError(duk_context *ctx, duk_int_t result);
};

#endif // HARIX_KERNEL_H
