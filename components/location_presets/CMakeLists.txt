set(LOCATION_PRESET_SRCS
    "location_presets.cpp"
)

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/location_presets_generated.cpp")
    list(APPEND LOCATION_PRESET_SRCS "location_presets_generated.cpp")
endif()

idf_component_register(
    SRCS ${LOCATION_PRESET_SRCS}
    INCLUDE_DIRS "."
    REQUIRES
        device_config
        nvs_flash
)
