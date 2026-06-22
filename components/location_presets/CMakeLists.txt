idf_component_register(
    SRCS "location_presets.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        device_config
        nvs_flash
)
