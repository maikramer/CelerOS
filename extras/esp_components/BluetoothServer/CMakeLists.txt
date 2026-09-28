set(srcs BluetoothServer.cpp BluetoothManager.cpp Commander.cpp BluetoothConnection.cpp CommandCode.cpp)
set(include_dirs .)
set(requires config esp-nimble-cpp JsonModels ErrorCodes Connection SafeContainers)

idf_component_register(SRCS "BluetoothServer.cpp" "${srcs}" INCLUDE_DIRS "${include_dirs}" REQUIRES "${requires}")