# UserManaging Component CMakeLists.txt
# Sistema moderno de autenticação local e cloud com integração Supabase

set(srcs
    AuthErrorCodes.cpp
    LocalAuthProvider.cpp
    CloudAuthProvider.cpp
    UserSession.cpp
    AuthManager.cpp
)

set(include_dirs .)

set(requires
    Storage
    Utility
    ErrorCodes
    Http
    Supabase
    json
)

set(priv_requires
    mbedtls
    nvs_flash
)

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES "${requires}"
    PRIV_REQUIRES "${priv_requires}"
)
