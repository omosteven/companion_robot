idf_component_register(
    SRCS
        "src/sound.cpp"

    INCLUDE_DIRS
        "include"

    REQUIRES 
        driver esp_driver_i2s esp_driver_gpio esp_timer
)