# Componente principal do KryonOS.
#
# FONTOs sao adicionados conforme os modulos sao portados do Arduino para o
# ESP-IDF (WiFi/httpd/OTA/FS ainda nao entram no build). O spike_main.cpp e
# o entry point provisorio ate main.cpp virar app_main().

idf_component_register(
    SRCS
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
        Http
        esp_https_ota
        app_update
        nvs_flash
        esp_wifi
        bblanchon__arduinojson
        lwip
        esp_netif
        duktape
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
