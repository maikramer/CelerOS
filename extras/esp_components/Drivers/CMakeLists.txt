set(srcs Battery.cpp Stepper.cpp)
set(include_dirs .)
set(requires JsonModels ErrorCodes Utility)

idf_component_register(SRCS "Stepper.cpp" "${srcs}" INCLUDE_DIRS "${include_dirs}" REQUIRES "${requires}")
