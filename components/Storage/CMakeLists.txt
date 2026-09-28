# So a classe NVS (wrapper tipado sobre nvs_flash) entra no CelerOS. O resto
# do Storage original (Storage::initialize monta SPIFFS em cima do /local,
# SdCard, Flash) esta em extras/esp_components/Storage.
idf_component_register(SRCS "NVS.cpp" INCLUDE_DIRS "." REQUIRES nvs_flash ErrorCodes)
