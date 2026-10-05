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
// Substitutos diretos do Serial.print/println: mesma saida roteada pelo sink
// (ring durante sessoes celerctl, logcat ao vivo, console quando livre)
void celer_log_println(const char* s);
void celer_log_print(const char* s);

// Print de APP JS (System.print): roteacao igual a do celer_log_printf com
// o pacote na frente ("[app:dogface]") e persistencia no apps.log (o
// celer_log_* comum e firmware e vai pro kern.log — ver LogPersist.h)
void celer_log_app(const char* pkg, const char* msg);

// Logcat (uso do HostLink)
void celer_logcat_set(bool on);
bool celer_logcat_active(void);
// Dump do ring SEM consumir (KL_LOG_DUMP / logcat --dump repetivel):
// tamanho em bytes e visita por blocos de ate 512 B
size_t celer_log_ring_size(void);
void celer_log_ring_forEach(void (*emit)(const char* chunk, size_t n));

#endif // CELER_LOG_SINK_H
