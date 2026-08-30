#include <stdio.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_camera.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"

#include "esp_wifi.h"

static const char *TAG = "robot_camera";

#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27

#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5

#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

#define UART_PORT_NUM UART_NUM_0
#define UART_BAUD_RATE 2000000

#define TX_BUF_SIZE 16384

static const uint8_t SYNC_MARKER[4] =
    {
        0xAA,
        0xBB,
        0xCC,
        0xDD};

static esp_err_t init_camera(void)
{
    camera_config_t config =
        {
            .pin_pwdn = CAM_PIN_PWDN,
            .pin_reset = CAM_PIN_RESET,
            .pin_xclk = CAM_PIN_XCLK,

            .pin_sscb_sda = CAM_PIN_SIOD,
            .pin_sscb_scl = CAM_PIN_SIOC,

            .pin_d7 = CAM_PIN_D7,
            .pin_d6 = CAM_PIN_D6,
            .pin_d5 = CAM_PIN_D5,
            .pin_d4 = CAM_PIN_D4,
            .pin_d3 = CAM_PIN_D3,
            .pin_d2 = CAM_PIN_D2,
            .pin_d1 = CAM_PIN_D1,
            .pin_d0 = CAM_PIN_D0,

            .pin_vsync = CAM_PIN_VSYNC,
            .pin_href = CAM_PIN_HREF,
            .pin_pclk = CAM_PIN_PCLK,

            .xclk_freq_hz = 20000000,

            .ledc_timer = LEDC_TIMER_0,
            .ledc_channel = LEDC_CHANNEL_0,

            .pixel_format = PIXFORMAT_JPEG,

            .frame_size = FRAMESIZE_QVGA,

            .jpeg_quality = 12,

            .fb_count = 2,

            .fb_location = CAMERA_FB_IN_PSRAM};

    return esp_camera_init(&config);
}

static void init_uart(void)
{
    uart_config_t uart_config =
        {
            .baud_rate = UART_BAUD_RATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT};

    ESP_ERROR_CHECK(
        uart_driver_install(
            UART_PORT_NUM,
            TX_BUF_SIZE,
            0,
            0,
            NULL,
            0));

    ESP_ERROR_CHECK(
        uart_param_config(
            UART_PORT_NUM,
            &uart_config));

    ESP_ERROR_CHECK(
        uart_set_pin(
            UART_PORT_NUM,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE));
}

void app_main(void)
{
    // stop bluetooth and wifi to save power
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) == ESP_OK)
    {
        esp_wifi_set_ps(WIFI_PS_MAX_MODEM); 
        esp_wifi_stop();                   
    }

    init_uart();

    if (init_camera() != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera initialization failed!");
        return;
    }

    ESP_LOGI(
        TAG,
        "Camera + UART initialized. UART=%d baud",
        UART_BAUD_RATE);

    while (true)
    {
        camera_fb_t *fb = esp_camera_fb_get();

        if (!fb)
        {
            ESP_LOGE(TAG, "Camera capture failed");

            vTaskDelay(pdMS_TO_TICKS(10));

            continue;
        }

        uint32_t img_len = (uint32_t)fb->len;

        uart_write_bytes(
            UART_PORT_NUM,
            (const char *)SYNC_MARKER,
            sizeof(SYNC_MARKER));

        uart_write_bytes(
            UART_PORT_NUM,
            (const char *)&img_len,
            sizeof(img_len));

        uart_write_bytes(
            UART_PORT_NUM,
            (const char *)fb->buf,
            fb->len);

        esp_camera_fb_return(fb);

        vTaskDelay(pdMS_TO_TICKS(40));
    }
}