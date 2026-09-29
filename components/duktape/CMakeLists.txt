idf_component_register(SRCS "duktape.c"
                       INCLUDE_DIRS ".")
# Builtins em ROM (DUK_USE_ROM_OBJECTS/STRINGS): as tabelas geradas omitem
# campos zerados de proposito (clen etc.)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
