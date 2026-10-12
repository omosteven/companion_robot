idf_component_register(
    SRCS
        "src/motor.cpp"

    INCLUDE_DIRS
        "include"

    REQUIRES 
        driver esp_driver_gpio  esp_driver_ledc esp_timer esp_driver_pcnt
)
