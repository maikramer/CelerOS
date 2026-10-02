idf_component_register(SRCS "duktape.c"
                       INCLUDE_DIRS ".")
# Builtins em ROM (DUK_USE_ROM_OBJECTS/STRINGS): as tabelas geradas omitem
# campos zerados de proposito (clen etc.)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
# Placas em -Os (CYD/devkit: slot de 1,6MB): o interpretador sobe para -O2
# sem inlining — medido no QEMU esp32 (-icount, JS de app: coordenadas,
# lista rolando, lacos, JSON, strings): 12-15% menos instrucoes por +19KB
# de flash. -O2 cheio dava 15-19% por +54KB (e -O1 10-13% por +16KB). As
# placas S3 ja compilam tudo em -O2 (CONFIG_COMPILER_OPTIMIZATION_PERF).
if(CONFIG_COMPILER_OPTIMIZATION_SIZE)
    target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -fno-inline-functions -fno-inline-small-functions)
endif()
