set(srcs ErrorCode.cpp GeneralErrorCodes.cpp NetworkErrorCodes.cpp WifiErrorCodes.cpp
        BluetoothErrorCodes.cpp FileErrorCodes.cpp StorageErrorCodes.cpp UserErrorCodes.cpp
        HardwareErrorCodes.cpp CommunicationErrorCodes.cpp)
set(include_dirs .)
# Removido JsonModels da lista de requires para evitar dependência circular
# ErrorCodes pode funcionar sem JsonModels, JsonModels é opcional
set(requires Utility config)

idf_component_register(SRCS "ErrorCode.cpp" "GeneralErrorCodes.cpp" "${srcs}" INCLUDE_DIRS "${include_dirs}" REQUIRES "${requires}")