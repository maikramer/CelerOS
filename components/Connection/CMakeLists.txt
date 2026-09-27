# Connection Component CMakeLists.txt
# NetworkManager system for WiFi connectivity management

set(srcs 
    # NetworkManager system
    NetworkManager.cpp
    NetworkCredentialStore.cpp
    NetworkSelector.cpp
    # Base classes for connection abstraction
    BaseConnection.cpp
    CommandCode.cpp
    Commander.cpp
)

set(include_dirs .)

# Note: Wifi is a PRIV_REQUIRES because it's only needed in .cpp files,
# not in public headers. This avoids circular dependency issues.
set(requires 
    Utility 
    config 
    JsonModels 
    ErrorCodes
    Storage
    esp_timer
)

set(priv_requires
    Wifi
)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${include_dirs} 
    REQUIRES ${requires}
    PRIV_REQUIRES ${priv_requires}
)

