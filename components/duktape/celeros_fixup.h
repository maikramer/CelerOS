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
