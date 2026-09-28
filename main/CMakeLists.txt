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
    "Kernel/AppRunner.cpp"
    "Kernel/TimeManager.cpp"
    "Launcher/LauncherUI.cpp"
    "Runtime/JSBindings.cpp"
    "Runtime/JsGfx.cpp"
    "Runtime/JsSystem.cpp"
    "Runtime/JsNet.cpp"
    "Runtime/JsFs.cpp"
    "Runtime/JsKeypad.cpp"
    "Runtime/JsSystemApps.cpp"
    "Settings/TouchCalibrator.cpp"
    "Utils/PinStore.cpp"
    "Utils/CelerSettings.cpp"
    "USBDevice/USBDevice.cpp"
    "USBDevice/CelerShell.cpp"
    "USBDevice/CelerLink.cpp"
    "USBDevice/SerialLink.cpp"
    "UI/Kui.cpp"
    "UI/Keyboard.cpp"
    "Launcher/Screens.cpp"
)
if(CONFIG_CELEROS_JS_GPIO)
    list(APPEND CELEROS_SRCS "Runtime/JsGpio.cpp")
endif()
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
elseif(CELEROS_BOARD STREQUAL "cyd-vspi")
    set(CELEROS_BOARD_DIR Boards/cyd-vspi)
else()
    message(FATAL_ERROR "CELEROS_BOARD='${CELEROS_BOARD}' desconhecida (opcoes: smartdisplay | cyd | cyd-vspi)")
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
        Network
        Http
        System
        esp_https_ota
        app_update
        espcoredump
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

# ---- Paginas web: o .html e a fonte; o build gera o .gz (servido com
# Content-Encoding: gzip) e embute so ele — ~4x menos flash que o texto cru.
idf_build_get_property(python PYTHON)
set(CELEROS_WEB_PAGES)
if(CONFIG_CELEROS_WEB_SERVER)
    set(CELEROS_WEB_PAGES WebManager/filemanager.html WebManager/ota_upload.html)
endif()
foreach(page ${CELEROS_WEB_PAGES})
    get_filename_component(page_name ${page} NAME)
    set(page_gz "${CMAKE_CURRENT_BINARY_DIR}/${page_name}.gz")
    add_custom_command(
        OUTPUT ${page_gz}
        COMMAND ${python} -c "import gzip,sys; open(sys.argv[2],'wb').write(gzip.compress(open(sys.argv[1],'rb').read(), 9, mtime=0))"
                "${CMAKE_CURRENT_SOURCE_DIR}/${page}" ${page_gz}
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${page}"
        VERBATIM)
    add_custom_target(celeros_web_${page_name} DEPENDS ${page_gz})
    add_dependencies(${COMPONENT_LIB} celeros_web_${page_name})
    target_add_binary_data(${COMPONENT_LIB} ${page_gz} BINARY)
endforeach()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    CELEROS_VERSION="1.2.0"
    CELEROS_API_LEVEL=6
)

# F3: app JS em task propria ("celerapp") — opt-in via Kconfig
if(CONFIG_CELEROS_APP_TASK)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CELEROS_APP_TASK=1)
endif()
