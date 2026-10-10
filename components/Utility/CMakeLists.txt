# Event.cpp removido - Event.h é um template header-only e não precisa de implementação em .cpp
# O Event.cpp antigo implementava NakedEvent que não existe mais
# Utility.cpp/CrossPlatformUtility.cpp removidos (triagem 2026-10, zero chamadores
# no repo inteiro): o Utility.cpp era o unico std::list da arvore e o
# CrossPlatformUtility so tinha o log_device, sem chamador. Singleton.h foi junto
# (nenhum "Singleton<" no repo). O componente ficou header-only.
# Removido ../submodules/nameof/include - submódulo não inicializado e não usado diretamente neste componente
# Se nameof for necessário, inicialize o submódulo: git submodule update --init --recursive
set(include_dirs .)
# Removido ErrorCodes da lista de requires - ErrorCodes depende de Utility, não o contrário
# Adicionado esp_driver_gpio para suporte a GPIO (ESP-IDF v6.0)
set(requires nvs_flash esp_driver_gpio)

idf_component_register(INCLUDE_DIRS "${include_dirs}" REQUIRES "${requires}")
