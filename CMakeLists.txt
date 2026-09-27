cmake_minimum_required(VERSION 3.16)

# Componentes vendados no repo (lib esp_components completa) que o KryonOS
# ainda nao usa — ficam de fora do build para nao arrastar deps de registry
# (nimble, lvgl, ...). Para ativar um: remover da lista e garantir as
# dependencias do idf_component.yml dele.
set(EXCLUDE_COMPONENTS
    BluetoothServer
    Drivers
    IoUtility
    SafeContainers
    Supabase
    UI
    UserManaging
    Time
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(KryonOS VERSION 1.0.0)
