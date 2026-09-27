#ifndef CELER_LOG_SINK_H
#define CELER_LOG_SINK_H

#include <stdarg.h>
#include <stddef.h>

// Roteador central de saida de texto do firmware (Serial.print do shim,
// logs do ESP_LOG via hook). Comportamento:
//   - sessao celerctl em andamento (modo link): a UART nao recebe texto
//     (evita corromper frames binarios), mas tudo e teclado num ring;
//   - logcat ativo: as linhas saem como frames KL_LOG_DATA pela ferramenta;
//   - sem nada conectado: segue para o vprintf normal do console.
void celer_log_vprintf(const char* fmt, va_list args);
void celer_log_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
bool celer_log_silent(void);

// Logcat (uso do CelerLink)
void celer_logcat_set(bool on);
bool celer_logcat_active(void);

#endif // CELER_LOG_SINK_H
