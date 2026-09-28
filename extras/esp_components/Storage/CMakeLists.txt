set(srcs "SdCard.cpp"
        "Flash.cpp"
        "NVS.cpp"
        "Storage.cpp")
set(include_dirs .)
# Removido UserManaging da lista de requires para evitar dependência circular
# Storage pode funcionar sem UserManaging, UserManaging é opcional
set(requires config Utility sdmmc vfs fatfs nvs_flash JsonModels)

idf_component_register(SRCS "${srcs}" INCLUDE_DIRS "${include_dirs}" REQUIRES "${requires}" spiffs)