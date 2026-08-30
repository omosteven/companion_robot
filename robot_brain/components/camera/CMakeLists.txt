idf_component_register(
    SRCS
        "src/camera.cpp"

    INCLUDE_DIRS
        "include"

    REQUIRES 
        driver esp_driver_uart esp_driver_gpio esp_timer
)