idf_component_register(
    SRCS 
        "main.cpp"
                       
    INCLUDE_DIRS 
        "."
        
    REQUIRES
        display
        neck
        camera
        wheel
        audio
        wifi_manager
        ota_manager
)