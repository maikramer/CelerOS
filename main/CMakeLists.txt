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
    "WebManager/CaptivePortal.cpp"
    "OTA/OtaManager.cpp"
    "Kernel/Core/HarixKernel.cpp"
    "Kernel/TimeManager.cpp"
    "Keyboard/MyKeyboard.cpp"
    "Launcher/LauncherUI.cpp"
    "Launcher/InstallerUI.cpp"
    "Launcher/AppStoreUI.cpp"
    "Launcher/HelpCenterUI.cpp"
    "Runtime/JSBindings.cpp"
    "Settings/SettingsUI.cpp"
    "Settings/TouchCalibrator.cpp"
    "WebServerApp/WebServerAppUI.cpp"
    "USBDevice/USBDevice.cpp"
    "USBDevice/KryonShell.cpp"
    "USBDevice/KryonLink.cpp"
    "USBDevice/SerialLink.cpp"
)
set(KRYONOS_PRIV_REQUIRES esp_driver_uart)
if(CONFIG_KRYONOS_USB_NATIVE)
    list(APPEND KRYONOS_PRIV_REQUIRES esp_tinyusb)
else()
    list(REMOVE_ITEM KRYONOS_SRCS "USBDevice/USBDevice.cpp")
endif()

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

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    KRYONOS_VERSION="1.1.0"
    KRYONOS_API_LEVEL=2
)

if(KRYONOS_BOARD STREQUAL "cyd")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KRYONOS_BOARD_CYD=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        KRYONOS_BOARD_SMARTDISPLAY_4IN=1
        KRYONOS_TOUCH_CAPACITIVE=1
    )
endif()
