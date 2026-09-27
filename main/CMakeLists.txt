# Componente principal do KryonOS.
#
# FONTOs sao adicionados conforme os modulos sao portados do Arduino para o
# ESP-IDF (WiFi/httpd/OTA/FS ainda nao entram no build). O spike_main.cpp e
# o entry point provisorio ate main.cpp virar app_main().

# USB device nativo (TinyUSB, CDC dupla) so para placas com GPIO19/20 livres
# e habilitacao explicita (KRYONOS_USB_NATIVE). Na SmartDisplay 4848S040 o
# GPIO19 e o SDA do touch GT911: o kryonctl roda na UART via SerialLink.
set(KRYONOS_SRCS
    "main.cpp"
    "Compat/KryonCompat.cpp"
    "FileSystem/FileSystem.cpp"
    "Display/Backlight.cpp"
    "Display/Icon.cpp"
    "WebManager/WebManager.cpp"
    "WebManager/WifiSetupPortal.cpp"
    "OTA/OtaManager.cpp"
    "Kernel/Core/HarixKernel.cpp"
    "Kernel/TimeManager.cpp"
    "Launcher/LauncherUI.cpp"
    "Launcher/InstallerUI.cpp"
    "Launcher/AppStoreUI.cpp"
    "Launcher/HelpCenterUI.cpp"
    "Runtime/JSBindings.cpp"
    "Settings/SettingsScreens.cpp"
    "Settings/TouchCalibrator.cpp"
    "WebServerApp/WebServerAppUI.cpp"
    "USBDevice/USBDevice.cpp"
    "USBDevice/KryonShell.cpp"
    "USBDevice/KryonLink.cpp"
    "USBDevice/SerialLink.cpp"
    "UI/Kui.cpp"
    "UI/Keyboard.cpp"
    "Launcher/Screens.cpp"
)
set(KRYONOS_PRIV_REQUIRES esp_driver_uart)
if(CONFIG_KRYONOS_USB_NATIVE)
    list(APPEND KRYONOS_PRIV_REQUIRES esp_tinyusb)
else()
    list(REMOVE_ITEM KRYONOS_SRCS "USBDevice/USBDevice.cpp")
endif()

# ---- HAL de placas: exatamente um Boards/<placa>/Board.cpp entra no build,
# e o diretorio da placa entra no include path (BoardDisplay.h resolve para
# a placa certa). Nenhum outro arquivo pode conter #ifdef de placa.
if(NOT KRYONOS_BOARD)
    set(KRYONOS_BOARD "smartdisplay" CACHE STRING "KryonOS target board: smartdisplay|cyd")
endif()
if(KRYONOS_BOARD STREQUAL "smartdisplay")
    set(KRYONOS_BOARD_DIR Boards/smartdisplay)
elseif(KRYONOS_BOARD STREQUAL "cyd")
    set(KRYONOS_BOARD_DIR Boards/cyd)
else()
    message(FATAL_ERROR "KRYONOS_BOARD='${KRYONOS_BOARD}' desconhecida (opcoes: smartdisplay | cyd)")
endif()
list(APPEND KRYONOS_SRCS "${KRYONOS_BOARD_DIR}/Board.cpp")

idf_component_register(
    SRCS
        ${KRYONOS_SRCS}
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
        ${KRYONOS_PRIV_REQUIRES}
)

target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/${KRYONOS_BOARD_DIR}")

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    KRYONOS_VERSION="1.1.0"
    KRYONOS_API_LEVEL=2
)
