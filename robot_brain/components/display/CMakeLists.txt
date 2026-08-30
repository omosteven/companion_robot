idf_component_register(
    SRCS
        "src/display.cpp"

    INCLUDE_DIRS
        "include"

    REQUIRES 
        driver
        esp_lcd
        esp_lcd_ili9488
        espressif__esp_new_jpeg
)

#    espressif__esp_jpeg