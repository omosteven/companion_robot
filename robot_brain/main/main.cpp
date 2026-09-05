#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "display.hpp"
#include "servo.hpp"
#include "camera.hpp"
#include "motor.hpp"

#include "colors.hpp"

#include "esp_crc.h"

Display display;
Servo neck_pan;
Camera camera;
Motor motor;

void camera_display_engine_task(void *pvParameters)
{
    ESP_LOGI(
        "VIDEO_ENG",
        "Low-latency camera display pipeline started.");

    uint32_t last_frame_crc = 0;
    size_t last_frame_size = 0;
    bool has_previous_frame = false;

    while (true)
    {
        size_t image_size = 0;
        uint32_t frame_number = 0;

        const uint8_t *frame_ptr =
            camera.acquireLatestFrame(
                image_size,
                frame_number);

        if (frame_ptr == nullptr || image_size == 0)
        {
            camera.releaseFrame();
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        uint32_t current_frame_crc =
            esp_crc32_le(
                0,
                frame_ptr,
                image_size);

        if (has_previous_frame &&
            image_size == last_frame_size &&
            current_frame_crc == last_frame_crc)
        {
            camera.releaseFrame();

            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        if (!camera.isValidJpeg(frame_ptr, image_size))
        {
            ESP_LOGW(
                "VIDEO_ENG",
                "Ignoring invalid JPEG frame #%lu",
                static_cast<unsigned long>(frame_number));

            camera.releaseFrame();

            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        esp_err_t err =
            display.draw_realtime_camera(
                frame_ptr,
                image_size, frame_number);

        if (err == ESP_OK)
        {
            last_frame_crc = current_frame_crc;
            last_frame_size = image_size;
            has_previous_frame = true;
        }
        else
        {
            ESP_LOGW(
                "VIDEO_ENG",
                "Failed to render frame #%lu: %s",
                static_cast<unsigned long>(frame_number),
                esp_err_to_name(err));
        }

        camera.releaseFrame();

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void encoder_track_task(void *pvParameters)
{
    int pulse_count_left_motor = 0;
    int pulse_count_right_motor = 0;
    while (true)
    {

        motor.read_encoder(&pulse_count_left_motor, &pulse_count_right_motor);
        ESP_LOGI("ENCODER", "ENCODER Motor data LEFT=%d RIGHT=%d", pulse_count_left_motor * -1, pulse_count_right_motor);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

extern "C" void app_main(void)
{

    vTaskDelay(
        pdMS_TO_TICKS(5000));

    esp_err_t display_err =
        display.init();

    if (display_err != ESP_OK)
    {
        ESP_LOGE(
            "MAIN",
            "Display initialization failed: %s",
            esp_err_to_name(display_err));

        return;
    }

    ESP_LOGI(
        "MAIN",
        "Display initialized successfully");

    display.draw_default_eyes(
        0,
        0);

    // esp_err_t servo_err =
    //     neck_pan.init(
    //         5,
    //         LEDC_LOW_SPEED_MODE,
    //         LEDC_CHANNEL_0);

    // if (servo_err != ESP_OK)
    // {
    //     ESP_LOGE(
    //         "MAIN",
    //         "Servo initialization failed: %s",
    //         esp_err_to_name(servo_err));

    //     return;
    // }

    // ESP_LOGI(
    //     "MAIN",
    //     "Servo hardware linked to GPIO 5 successfully");

    // esp_err_t camera_err =
    //     camera.init();

    // if (camera_err != ESP_OK)
    // {
    //     ESP_LOGE(
    //         "MAIN",
    //         "Camera initialization failed: %s",
    //         esp_err_to_name(camera_err));

    //     return;
    // }

    // if (camera.startTask() != ESP_OK)
    // {
    //     ESP_LOGE(
    //         "MAIN",
    //         "Failed to start camera RX task");

    //     return;
    // }

    // ESP_LOGI(
    //     "MAIN",
    //     "Camera UART receiver started");

    esp_err_t motor_err =
        motor.init();

    if (motor_err != ESP_OK)
    {
        ESP_LOGE(
            "MAIN",
            "Motor initialization failed: %s",
            esp_err_to_name(motor_err));

        return;
    }
    // BaseType_t task_result =
    //     xTaskCreatePinnedToCore(
    //         camera_display_engine_task,
    //         "video_render_engine",
    //         8192,
    //         nullptr,
    //         6,
    //         nullptr,
    //         1);

    // if (task_result != pdPASS)
    // {
    //     ESP_LOGE(
    //         "MAIN",
    //         "Failed to create video render task");

    //     return;
    // }

    ESP_LOGI(
        "MAIN",
        "Video render task started");

    BaseType_t task_result =
        xTaskCreatePinnedToCore(
            motor.run_task,
            "encoder_task",
            8192,
            nullptr,
            6,
            nullptr,
            1);

    if (task_result != pdPASS)
    {
        ESP_LOGE(
            "MAIN",
            "Failed to create encoder task");

        return;
    }

    while (true)
    {

        ESP_LOGI(
            "MOTOR",
            "Moving Forward");

        motor.send_to_queue(600, 600, Motor::MovementType::STRAIGHT, 2000);

        vTaskDelay(
            pdMS_TO_TICKS(1000));
        // ESP_LOGI(
        //     "MOTOR",
        //     "Moving Left");
        // motor.turn_left(600, 2000);

        // vTaskDelay(
        //     pdMS_TO_TICKS(1000));

        // ESP_LOGI(
        //     "MOTOR",
        //     "Moving Right");
        // motor.turn_right(600, 2000);

        // vTaskDelay(
        //     pdMS_TO_TICKS(1000));

        ESP_LOGI(
            "MOTOR",
            "Moving Reverse");
        motor.send_to_queue(600, 600, Motor::MovementType::REVERSE, 2000);

        vTaskDelay(
            pdMS_TO_TICKS(1000));

        // ESP_LOGI(
        //     "MOTOR",
        //     "Moving Spin");
        // motor.spin(600, 2000);

        // vTaskDelay(
        //     pdMS_TO_TICKS(1000));
        // motor.set_motor_speed(100, motor.Wheel::LEFT);
        // motor.set_motor_direction(motor.MotorDirection::FORWARD, motor.Wheel::LEFT);

        // motor.set_motor_speed(100, motor.Wheel::RIGHT);
        // motor.set_motor_direction(motor.MotorDirection::FORWARD, motor.Wheel::RIGHT);

        // vTaskDelay(
        //     pdMS_TO_TICKS(3000));

        //      ESP_LOGI(
        // "MOTOR",
        // "Moving Backward");
        // motor.set_motor_speed(100, motor.Wheel::LEFT);
        // motor.set_motor_direction(motor.MotorDirection::BACKWARD, motor.Wheel::LEFT);

        // motor.set_motor_speed(100, motor.Wheel::RIGHT);
        // motor.set_motor_direction(motor.MotorDirection::BACKWARD, motor.Wheel::RIGHT);
        // vTaskDelay(
        //     pdMS_TO_TICKS(3000));
    }
}
