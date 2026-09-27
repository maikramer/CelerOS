# Componente principal do KryonOS.
#
# FONTOs sao adicionados conforme os modulos sao portados do Arduino para o
# ESP-IDF (WiFi/httpd/OTA/FS ainda nao entram no build). O spike_main.cpp e
# o entry point provisorio ate main.cpp virar app_main().

idf_component_register(
    SRCS
        "spike_main.cpp"
        "Compat/KryonCompat.cpp"
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
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    KRYONOS_VERSION="1.0.0"
    KRYONOS_API_LEVEL=1
    KRYONOS_BOARD_SMARTDISPLAY_4IN=1
)
