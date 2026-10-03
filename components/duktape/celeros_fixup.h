/* Fixup do duk_config.h (gerado com --fixup-header-file): declara o hook de
 * timeout de execucao (DUK_USE_EXEC_TIMEOUT_CHECK) no escopo do duktape.c.
 * O guarda extern "C" e essencial: duktape.c e C, o CelerKernel.cpp e C++.
 */
#ifdef __cplusplus
extern "C" {
#endif

extern duk_bool_t celer_exec_timeout_check(void *udata);

#ifdef __cplusplus
}
#endif

/* Debugger do Duktape por placa (Kconfig CELEROS_JS_DEBUGGER): o yaml liga
 * DUK_USE_DEBUGGER_SUPPORT e o recorte acontece AQUI, onde o sdkconfig da
 * placa e visivel. Ligado, cada funcao compilada ganha a propriedade
 * fileName — sem ela o executor nunca ativa breakpoint (o match e por
 * fileName da funcao) e o Status chega com arquivo undefined. Custo: uma
 * propriedade por funcao/closure, por isso so nas placas com PSRAM.
 */
#include "sdkconfig.h"
#if defined(CONFIG_CELEROS_JS_DEBUGGER) && CONFIG_CELEROS_JS_DEBUGGER
#define DUK_USE_FUNC_FILENAME_PROPERTY
/* Erro que derrubaria o app pausa no ponto do throw (pilha e locais
 * intactos para inspecao); o pcall do kernel e C, nao conta como catcher */
#define DUK_USE_DEBUGGER_PAUSE_UNCAUGHT
/* identifica o alvo na linha de versao do handshake do debugger */
#undef DUK_USE_TARGET_INFO
#define DUK_USE_TARGET_INFO "CelerOS"
#else
#undef DUK_USE_DEBUGGER_SUPPORT
#endif
