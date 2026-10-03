#include "JsDebugger.h"
#include "HostLink.h"
#include "LogSink.h"

#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "esp_task_wdt.h"

// ---------------------------------------------------------------------------
// Debug do Duktape sobre o canal do celerctl. O protocolo do debugger e
// binario (dmsg) e viaja inteiro dentro de frames KL_DEBUG_DATA; as duas
// pontas vivem em tasks DIFERENTES (dbg_link produz, a task do app consome
// dentro do executor), entao o sentido host->device e um StreamBuffer.
// O executor checa o debugger pelo MESMO interrupt counter do exec-timeout:
// pausado (attach, breakpoint, step) ele fica no read esperando comandos, e
// com o debugger attachado o exec-timeout nao dispara (pausa e laco
// inspecionado nao viram RangeError).
//
// Suporte por placa: Kconfig CELEROS_JS_DEBUGGER (celeros_fixup.h liga o
// DUK_USE_DEBUGGER_SUPPORT e o fileName por funcao que os breakpoints
// exigem). O handshake e UNILATERAL: duk_debugger_attach so ESCREVE a linha de
// versao do target, nunca le nada do cliente — qualquer byte "de versao"
// do cliente vira initial byte invalido e derruba a sessao (Detaching 1).
// Por isso o gate do attach e a flag s_client (KL_DEBUG_CTL do proxy depois
// do accept), nunca bytes no buffer. Sem o suporte tudo vira stub e a flag
// `debug on` avisa.
// ---------------------------------------------------------------------------

#if defined(DUK_USE_DEBUGGER_SUPPORT)

namespace {

constexpr size_t K_DBG_RX = 4096;  // host -> device (mensagens do cliente)

volatile bool s_request = false;
volatile bool s_client = false;  // proxy com cliente TCP conectado
volatile bool s_attached = false;
duk_context* s_ctx = nullptr;
StreamBufferHandle_t s_rx = nullptr;

// Bloqueante enquanto o cliente existir: read devolvendo 0 significa ERRO
// DE TRANSPORTE (o debugger desattacha). Pausado num breakpoint o executor
// fica aqui esperando o proximo comando — o TWDT da task do app e
// alimentado a cada fatia. Cliente sumiu (DEBUG_CTL 0): devolve 0, o
// Duktape desattacha e o app volta a rodar em vez de ficar pendurado.
size_t dbgRead(void* udata, char* buf, size_t len) {
    (void)udata;
    if (s_rx == nullptr || len == 0) return 0;
    while (s_client) {
        size_t got = xStreamBufferReceive(s_rx, buf, len, pdMS_TO_TICKS(200));
        if (got > 0) return got;
        esp_task_wdt_reset();
    }
    return 0;
}

// Dados disponiveis sem consumir (o executor pergunta a cada cooperate).
duk_size_t dbgPeek(void* udata) {
    (void)udata;
    if (s_rx == nullptr) return 0;
    return xStreamBufferBytesAvailable(s_rx);
}

// Device -> host: frame KL_DEBUG_DATA pelo canal da sessao ativa. Sem
// cliente (proxy fechou) e erro de transporte: o Duktape desattacha em vez
// de mandar frames binarios para um canal que ja voltou a ser console.
duk_size_t dbgWrite(void* udata, const char* buf, size_t len) {
    (void)udata;
    if (!s_client) return 0;
    if (!HostLink::sendDebugFrame((const uint8_t*)buf, len)) return 0;
    return len;
}

// Sessao caiu (cliente desconectou/erro de transporte): o estado local
// acompanha — duk_debugger_attached() nao e API publica no 2.7.
void dbgDetached(duk_context* ctx, void* udata) {
    (void)ctx; (void)udata;
    s_attached = false;
    s_ctx = nullptr;
    celer_log_println("debugger JS: sessao encerrada");
}

}  // namespace

namespace JsDebugger {

void setRequested(bool on) {
    s_request = on;
    if (!on) s_client = false;
    celer_log_printf("debugger JS: %s (vale para o proximo app lancado)\n",
                     on ? "armado" : "desarmado");
}

bool requested() { return s_request; }

void setClient(bool on) {
    s_client = on;
    s_request = on;
    celer_log_printf("debugger JS: cliente %s\n", on ? "conectado (attach no proximo app/yield)" : "saiu");
}

bool attach(duk_context* ctx) {
    if (ctx == nullptr) return false;
    if (s_rx == nullptr) {
        s_rx = xStreamBufferCreate(K_DBG_RX, 1);
        if (s_rx == nullptr) return false;
    }
    // o attach PAUSA o app e o executor passa a esperar comandos: sem
    // cliente do outro lado o app ficaria parado. So attacha com o proxy
    // avisando que ha cliente — o runFile tenta primeiro e o present() do
    // JSBindings da a segunda chance no proximo yield do app
    if (!s_request || !s_client) return false;
    // o cliente so fala depois da linha de versao: o que estiver no buffer
    // e resto da sessao anterior (comando mandado depois do detach) e viraria
    // a 1a mensagem desta
    xStreamBufferReset(s_rx);
    duk_debugger_attach(ctx, dbgRead, dbgWrite, dbgPeek,
                        nullptr, nullptr, nullptr, dbgDetached, nullptr);
    s_ctx = ctx;
    s_attached = true;
    celer_log_println("debugger JS: attachado (celerctl debug no outro lado)");
    return true;
}

void detach(duk_context* ctx) {
    if (ctx == nullptr || !s_attached || ctx != s_ctx) return;
    duk_debugger_detach(ctx);
    s_attached = false;
    s_ctx = nullptr;
    celer_log_println("debugger JS: desattachado");
}

bool maybeAttach(duk_context* ctx) {
    if (ctx == nullptr) return false;
    // proxy fechou com o app rodando: desattacha aqui, na task do app (o
    // Duktape nao e thread-safe — o link so derruba a flag)
    if (s_attached && !s_client) {
        detach(ctx);
        return false;
    }
    if (s_attached || !s_request) return false;
    return attach(ctx);
}

bool attached(duk_context* ctx) {
    return s_attached && ctx != nullptr && ctx == s_ctx;
}

void feedHost(const uint8_t* data, size_t n) {
    if (data == nullptr || n == 0) return;
    // o buffer pode ser criado aqui (task do link): comandos que chegarem
    // antes do attach esperam no buffer
    if (s_rx == nullptr) s_rx = xStreamBufferCreate(K_DBG_RX, 1);
    if (s_rx == nullptr) return;
    xStreamBufferSend(s_rx, data, n, pdMS_TO_TICKS(100));
}

}  // namespace JsDebugger

#else  // !DUK_USE_DEBUGGER_SUPPORT — stubs (infra compilada, suporte off)

namespace JsDebugger {

void setRequested(bool) {
    celer_log_println("debugger JS: suporte nao compilado neste firmware "
                      "(Kconfig CELEROS_JS_DEBUGGER)");
}
bool requested() { return false; }
void setClient(bool) {}
bool attach(duk_context*) { return false; }
void detach(duk_context*) {}
bool maybeAttach(duk_context*) { return false; }
bool attached(duk_context*) { return false; }
void feedHost(const uint8_t*, size_t) {}

}  // namespace JsDebugger

#endif  // DUK_USE_DEBUGGER_SUPPORT
