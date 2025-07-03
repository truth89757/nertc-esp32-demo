idf_component_register(
    INCLUDE_DIRS "include"
)

add_prebuilt_library(nertc_wake_up "${CMAKE_CURRENT_SOURCE_DIR}/libs/${CONFIG_IDF_TARGET}/libnertc_wake_up.a"
                    REQUIRES mbedtls lwip espressif__esp-sr)
target_link_libraries(${COMPONENT_LIB} INTERFACE nertc_wake_up)
