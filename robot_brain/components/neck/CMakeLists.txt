idf_component_register(
    SRCS
        "src/servo.cpp"

    INCLUDE_DIRS
        "include"

    REQUIRES 
        esp_driver_ledc
)