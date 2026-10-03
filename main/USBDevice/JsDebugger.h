#ifndef JS_DEBUGGER_H
#define JS_DEBUGGER_H

#include <stddef.h>
#include <stdint.h>

#include <duktape.h>

// Transporte do Duktape debugger sobre o HostLink (celerctl debug):
//   host -> device: frames KL_DEBUG_DATA alimentam um StreamBuffer, consumido
//                   pelo read/peek callbacks NA TASK DO APP
//   device -> host: write callback sai como frames KL_DEBUG_DATA pela sessao
//                   ativa (mesmo padrao thread-safe do logcat)
// A flag "requested" (shell `debug on/off` / KL_DEBUG_CTL) arma o attach do
// PROXIMO app: o CelerKernel chama attach() ao criar o heap e detach() antes
// de destrui-lo. Enquanto o debugger esta attachado, o exec-timeout renova a
// janela (pausa em breakpoint nao vira RangeError e o TWDT e alimentado).
namespace JsDebugger {

// Liga a flag: o proximo app lancado attacha o debugger (respondida pelo
// KL_DEBUG_CTL; o shell `debug on/off` chama direto)
void setRequested(bool on);
bool requested();
// Proxy do celerctl com cliente TCP conectado (KL_DEBUG_CTL 1/0): gate do
// attach. Ligar tambem arma a flag; desligar desarma e solta um app pausado.
void setClient(bool on);
// main.js do app que o kernel vai rodar (AppRequest "restart"/"info").
void noteApp(const char* mainPath);

// Registra os callbacks de transporte no heap recem-criado do app. False se
// o stream buffer nao puder ser alocado OU se nao ha cliente conectado (o
// attach pausa o app esperando comandos — nunca attachar "as cegas").
// Sem DUK_USE_DEBUGGER_SUPPORT tudo abaixo e stub (attached sempre false).
bool attach(duk_context* ctx);
// Chamado a cada yield do app (present): attacha quando a flag esta armada
// E o cliente conectou (o app ja rodava); desattacha se o cliente saiu.
bool maybeAttach(duk_context* ctx);
// Saida limpa antes do duk_destroy_heap (idempotente).
void detach(duk_context* ctx);
// O debugger do ctx esta attachado (guard do exec-timeout).
bool attached(duk_context* ctx);

// KL_DEBUG_DATA do host: produz no stream buffer (task do link).
void feedHost(const uint8_t* data, size_t n);

}  // namespace JsDebugger

#endif  // JS_DEBUGGER_H
