#ifndef KRYON_COMPAT_ARDUINO_H
#define KRYON_COMPAT_ARDUINO_H

// ============================================================================
// Shim minimo de Arduino.h para o KryonOS em ESP-IDF puro.
//
// NAO e uma camada Arduino: so existem aqui as primitivas que o codigo do
// KryonOS usa (delay/millis, GPIO, Serial/ESP.* , map/constrain). Nao ha
// classe String — o projeto usa std::string (Utils/StrUtils.h).
//
// Este header vive em main/Compat/ e e privado do componente main, portanto
// nao vaza para os componentes (LovyanGFX, duktape, ...).
// ============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <utility>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

// --- Constantes de nivel GPIO (valores identicos ao arduino-esp32) ---------
#define PROGMEM

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define PULLUP 0x04
#define INPUT_PULLUP 0x05
#define PULLDOWN 0x08
#define INPUT_PULLDOWN 0x09

// --- Tempo ------------------------------------------------------------------
uint32_t millis(void);
uint32_t micros(void);
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);

// --- GPIO -------------------------------------------------------------------
void pinMode(int pin, uint8_t mode);
void digitalWrite(int pin, uint8_t val);
int digitalRead(int pin);
int analogRead(int pin);          // adc_oneshot, 12 bits
void analogWrite(int pin, int val); // LEDC 8 bits (0-255)
unsigned long pulseIn(int pin, uint8_t state, unsigned long timeout_us);

// --- Matematica / utilidades Arduino ---------------------------------------
long map(long x, long in_min, long in_max, long out_min, long out_max);
#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif
inline void yield(void) { taskYIELD(); }

// --- Temperatura interna do chip (driver temperature_sensor) ---------------
float temperatureRead(void);

// --- Serial (console UART0 via printf) --------------------------------------
class KryonSerial {
public:
    void begin(unsigned long baud) { (void)baud; }
    void print(const char* s)      { printf("%s", s); }
    void print(const std::string& s) { printf("%s", s.c_str()); }
    void print(char c)             { putchar(c); }
    void print(int v)              { printf("%d", v); }
    void print(unsigned int v)     { printf("%u", v); }
    void print(long v)             { printf("%ld", v); }
    void print(unsigned long v)    { printf("%lu", v); }
    void print(float v)            { printf("%f", v); }
    void print(double v)           { printf("%f", v); }
    void println(void)             { printf("\n"); }
    void println(const char* s)    { printf("%s\n", s); }
    void println(const std::string& s) { printf("%s\n", s.c_str()); }
    void println(char c)           { printf("%c\n", c); }
    void println(int v)            { printf("%d\n", v); }
    void println(unsigned int v)   { printf("%u\n", v); }
    void println(long v)           { printf("%ld\n", v); }
    void println(unsigned long v)  { printf("%lu\n", v); }
    void println(float v)          { printf("%f\n", v); }
    void println(double v)         { printf("%f\n", v); }
    void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
};
extern KryonSerial Serial;

// --- ESP.* (info do chip / heap / restart) ----------------------------------
class KryonEsp {
public:
    void restart(void);
    uint32_t getHeapSize(void);
    uint32_t getFreeHeap(void);
    uint32_t getMinFreeHeap(void);
    uint32_t getMaxAllocHeap(void);
    uint32_t getPsramSize(void);
    uint32_t getFreePsram(void);
    uint32_t getCpuFreqMHz(void);
    const char* getChipModel(void);
    uint8_t getChipCores(void);
    uint8_t getChipRevision(void);
    uint32_t getFlashChipSize(void);
};
extern KryonEsp ESP;

#endif // KRYON_COMPAT_ARDUINO_H
