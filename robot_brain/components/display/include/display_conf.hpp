#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"


namespace DisplayConfig
{

    // -------------------------------------------------
    // Display
    // -------------------------------------------------

    constexpr int WIDTH  = 480;
    constexpr int HEIGHT = 320;


    // -------------------------------------------------
    // SPI
    // -------------------------------------------------

    constexpr spi_host_device_t SPI_HOST = SPI2_HOST;

    constexpr gpio_num_t PIN_SCLK = GPIO_NUM_13;

    constexpr gpio_num_t PIN_MOSI = GPIO_NUM_12;

    // LCD MISO is intentionally not connected yet.
    // constexpr gpio_num_t PIN_MISO = GPIO_NUM_NC;
    constexpr gpio_num_t PIN_MISO = GPIO_NUM_14;



    // -------------------------------------------------
    // LCD control
    // -------------------------------------------------

    constexpr gpio_num_t PIN_CS = GPIO_NUM_11;

    constexpr gpio_num_t PIN_DC = GPIO_NUM_10;

    constexpr gpio_num_t PIN_RST = GPIO_NUM_9;


    // -------------------------------------------------
    // Backlight
    // -------------------------------------------------

    // Backlight is initially powered externally from 3.3 V.
    constexpr gpio_num_t PIN_LED = GPIO_NUM_NC;


    // -------------------------------------------------
    // SPI configuration
    // -------------------------------------------------

    constexpr int SPI_FREQUENCY_HZ =
        60 * 1000 * 1000;

    constexpr int TRANS_QUEUE_DEPTH = 10;

}
