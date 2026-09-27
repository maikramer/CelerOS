set(srcs "JsonModels.cpp")
set(include_dirs .)
# Removido UserManaging da lista de requires para evitar dependência circular
# JsonModels pode funcionar sem UserManaging, UserManaging é opcional
# Nota: No REQUIRES usamos 'nlohmann-json' (nome do componente), não 'johboh/nlohmann-json' (nome no registry)
set(requires Utility config nlohmann-json ErrorCodes)

idf_component_register(SRCS "${srcs}" INCLUDE_DIRS "${include_dirs}" REQUIRES "${requires}")