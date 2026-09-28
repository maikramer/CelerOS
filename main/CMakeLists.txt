# Componente principal do CelerOS.
#
# FONTOs sao adicionados conforme os modulos sao portados do Arduino para o
# ESP-IDF (WiFi/httpd/OTA/FS ainda nao entram no build). O spike_main.cpp e
# o entry point provisorio ate main.cpp virar app_main().

# USB device nativo (TinyUSB, CDC dupla) so para placas com GPIO19/20 livres
# e habilitacao explicita (CELEROS_USB_NATIVE). Na SmartDisplay 4848S040 o
# GPIO19 e o SDA do touch GT911: o celerctl roda na UART via SerialLink.
set(CELEROS_SRCS
    "main.cpp"
    "Compat/CelerCompat.cpp"
    "FileSystem/FileSystem.cpp"
    "Display/Backlight.cpp"
    "Display/Icon.cpp"
    "WebManager/WebManager.cpp"
    "WebManager/WebAuth.cpp"
    "WebManager/WifiSetupPortal.cpp"
    "OTA/OtaManager.cpp"
    "Kernel/Core/CelerKernel.cpp"
    "Kernel/TimeManager.cpp"
    "Launcher/LauncherUI.cpp"
    "Runtime/JSBindings.cpp"
    "Settings/TouchCalibrator.cpp"
    "Utils/PinStore.cpp"
    "USBDevice/USBDevice.cpp"
    "USBDevice/CelerShell.cpp"
    "USBDevice/CelerLink.cpp"
    "USBDevice/SerialLink.cpp"
    "UI/Kui.cpp"
    "UI/Keyboard.cpp"
    "Launcher/Screens.cpp"
)
set(CELEROS_PRIV_REQUIRES esp_driver_uart)
if(CONFIG_CELEROS_USB_NATIVE)
    list(APPEND CELEROS_PRIV_REQUIRES esp_tinyusb)
else()
    list(REMOVE_ITEM CELEROS_SRCS "USBDevice/USBDevice.cpp")
endif()

# ---- HAL de placas: exatamente um Boards/<placa>/Board.cpp entra no build,
# e o diretorio da placa entra no include path (BoardDisplay.h resolve para
# a placa certa). Nenhum outro arquivo pode conter #ifdef de placa.
if(NOT CELEROS_BOARD)
    set(CELEROS_BOARD "smartdisplay" CACHE STRING "CelerOS target board: smartdisplay|cyd")
endif()
if(CELEROS_BOARD STREQUAL "smartdisplay")
    set(CELEROS_BOARD_DIR Boards/smartdisplay)
elseif(CELEROS_BOARD STREQUAL "cyd")
    set(CELEROS_BOARD_DIR Boards/cyd)
else()
    message(FATAL_ERROR "CELEROS_BOARD='${CELEROS_BOARD}' desconhecida (opcoes: smartdisplay | cyd)")
endif()
list(APPEND CELEROS_SRCS "${CELEROS_BOARD_DIR}/Board.cpp")

idf_component_register(
    SRCS
        ${CELEROS_SRCS}
    INCLUDE_DIRS
        "."
        "Compat"
    REQUIRES
        LovyanGFX
        driver
        esp_adc
        esp_driver_tsens
        esp_timer
        spi_flash
        esp_hw_support
        fatfs
        sdmmc
        joltwallet__esp_littlefs
        mbedtls
        Wifi
        Connection
        Http
        System
        esp_https_ota
        app_update
        nvs_flash
        esp_wifi
        bblanchon__arduinojson
        lwip
        esp_netif
        duktape
    PRIV_REQUIRES
        ${CELEROS_PRIV_REQUIRES}
)

target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/${CELEROS_BOARD_DIR}")

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    CELEROS_VERSION="1.2.0"
    CELEROS_API_LEVEL=6
)
