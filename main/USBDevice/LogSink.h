#ifndef KRYON_LOG_SINK_H
#define KRYON_LOG_SINK_H

#include <stdarg.h>

// Roteador central de saida de texto do firmware (Serial.print do shim e
// afins). Quando o canal serial esta ocupado com frames do kryonctl (modo
// link), a saida e descartada para nao corromper transferencias binarias;
// no futuro o logcat (W6c) tecla aqui para envia-los pela ferramenta.
void kryon_log_vprintf(const char* fmt, va_list args);
bool kryon_log_silent(void);

#endif // KRYON_LOG_SINK_H
