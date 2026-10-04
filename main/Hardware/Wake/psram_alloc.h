// Injetado (-include) SO nos fontes do microfrontend vendorizado
// (Hardware/Wake/frontend): as alocacoes dele sao pequenas (< 4 KB) e o
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL as mandava para a RAM interna — ~12 KB
// que faltavam ao DMA do alto-falante e do BLE no cao. Nada aqui e acessado
// com cache desligado: PSRAM serve. Sem PSRAM cai no malloc normal.
#pragma once
#include <stdlib.h>
#include "esp_heap_caps.h"

static inline void* celer_wake_malloc(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p != NULL ? p : malloc(n);
}
static inline void* celer_wake_calloc(size_t n, size_t sz) {
    void* p = heap_caps_calloc(n, sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p != NULL ? p : calloc(n, sz);
}
static inline void* celer_wake_realloc(void* q, size_t n) {
    void* p = heap_caps_realloc(q, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p != NULL ? p : realloc(q, n);
}

#define malloc(n) celer_wake_malloc(n)
#define calloc(n, sz) celer_wake_calloc(n, sz)
#define realloc(q, n) celer_wake_realloc(q, n)
