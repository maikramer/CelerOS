# Event.cpp removido - Event.h é um template header-only e não precisa de implementação em .cpp
# O Event.cpp antigo implementava NakedEvent que não existe mais
set(srcs Utility.cpp CrossPlatformUtility.cpp)
# Removido ../submodules/nameof/include - submódulo não inicializado e não usado diretamente neste componente
# Se nameof for necessário, inicialize o submódulo: git submodule update --init --recursive
set(include_dirs .)
# Removido ErrorCodes da lista de requires - ErrorCodes depende de Utility, não o contrário
# Nota: No REQUIRES usamos 'nlohmann-json' (nome do componente), não 'johboh/nlohmann-json' (nome no registry)
# Adicionado esp_driver_gpio para suporte a GPIO (ESP-IDF v6.0)
set(requires nvs_flash nlohmann-json esp_driver_gpio)

idf_component_register(SRCS "${srcs}" INCLUDE_DIRS "${include_dirs}" REQUIRES "${requires}")
