# Wifi Component CMakeLists.txt

set(srcs 
    WifiConnection.cpp
    WifiClient.cpp
    WifiServer.cpp
    WifiOta.cpp
    WifiTelnet.cpp
    OtaManager.cpp
    Telnet.cpp
    IPAddress.cpp
    WirelessDevice.cpp
    WifiAP.cpp
    CaptivePortal.cpp
)

set(include_dirs .)

set(requires 
    esp_wifi 
    nvs_flash 
    esp_netif 
    esp_event 
    Utility 
    esp_https_ota 
    bootloader_support 
    esp_http_client 
    esp_http_server
    efuse 
    Connection 
    ErrorCodes 
    Storage
    esp_timer
    bootloader_support
    lwip
    Http
    nlohmann-json
)

idf_component_register(
    SRCS "WifiConnection.cpp" "${srcs}" 
    INCLUDE_DIRS "${include_dirs}" 
    REQUIRES "${requires}"
)
