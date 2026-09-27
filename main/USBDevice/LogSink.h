#ifndef KRYON_LOG_SINK_H
#define KRYON_LOG_SINK_H

#include <stdarg.h>
#include <stddef.h>

// Roteador central de saida de texto do firmware (Serial.print do shim,
// logs do ESP_LOG via hook). Comportamento:
//   - sessao kryonctl em andamento (modo link): a UART nao recebe texto
//     (evita corromper frames binarios), mas tudo e teclado num ring;
//   - logcat ativo: as linhas saem como frames KL_LOG_DATA pela ferramenta;
//   - sem nada conectado: segue para o vprintf normal do console.
void kryon_log_vprintf(const char* fmt, va_list args);
void kryon_log_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
bool kryon_log_silent(void);

// Logcat (uso do KryonLink)
void kryon_logcat_set(bool on);
bool kryon_logcat_active(void);

#endif // KRYON_LOG_SINK_H
