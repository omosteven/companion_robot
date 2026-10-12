idf_component_register(
    SRCS
        "src/ota_manager.cpp"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_http_server
        app_update
)