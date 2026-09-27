# IoUtility Component CMakeLists.txt
# Utilitários de I/O: Button wrapper, LED Blinker

set(srcs 
    Button.cpp
    LedBlinker.cpp
)

set(include_dirs .)

set(requires 
    JsonModels 
    ErrorCodes
    Utility
)

set(priv_requires
    button
)

idf_component_register(
    SRCS "${srcs}" 
    INCLUDE_DIRS "${include_dirs}" 
    REQUIRES "${requires}"
    PRIV_REQUIRES "${priv_requires}"
)
