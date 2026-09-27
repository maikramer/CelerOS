cmake_minimum_required(VERSION 3.16)

# Componentes vendados no repo (lib esp_components completa) que o CelerOS
# ainda nao usa — ficam de fora do build para nao arrastar deps de registry
# (nimble, lvgl, ...). Para ativar um: remover da lista e garantir as
# dependencias do idf_component.yml dele.
# Placa alvo: "smartdisplay" (Guition S3 4848S040) ou "cyd" (ESP32-2432S028R).
# Passar no configure: idf.py -DCELEROS_BOARD=cyd ...
set(CELEROS_BOARD "smartdisplay" CACHE STRING "CelerOS target board: smartdisplay|cyd")

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
project(CelerOS VERSION 1.2.0)
