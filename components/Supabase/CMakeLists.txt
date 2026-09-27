idf_component_register(
    SRCS 
        "SupabaseClient.cpp"
        "SupabaseQuery.cpp"
    INCLUDE_DIRS "."
    REQUIRES 
        Http
        Storage
        ErrorCodes
)
