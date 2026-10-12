#include <cstdio>
#include <vector>
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "display.hpp"
#include "servo.hpp"
#include "colors.hpp"
#include "motor.hpp"
#include "camera.hpp"
#include "sound.hpp"

// #include "wifi_manager.hpp"
// #include "ota_manager.hpp"

#include "esp_crc.h"

#include <cmath>
#include <algorithm>
#include "esp_timer.h"
#include <inttypes.h>

Display display;
Servo neck_pan;
Camera camera;
Motor motor;
Sound sound;

void camera_display_task(void *pvParameters)
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
              ESP_LOGE("CAMERA_ERROR", "Acquire failed %d", image_size);
            vTaskDelay(pdMS_TO_TICKS(10));
            
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
//   ESP_LOGE("CAMERA_ERROR", "Prev==Current failed" );
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        if (!camera.isValidJpeg(frame_ptr, image_size))
        {
            ESP_LOGW(
                "VIDEO_ENG",
                "Ignoring invalid JPEG frame #%lu",
                static_cast<unsigned long>(frame_number));

                //   ESP_LOGE("CAMERA_ERROR", "Jpegvalid failed" );
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

void test_record_playback_loop(Sound &sound)
{
    constexpr size_t TEST_SECONDS = 3;
    constexpr size_t SAMPLE_RATE = Sound::SAMPLE_RATE;
    constexpr size_t TOTAL_FRAMES = SAMPLE_RATE * TEST_SECONDS;
    constexpr size_t DEBUG_FRAMES = 16;
    constexpr const char *TAG = "AudioLoop";
    ESP_LOGW(TAG, "Target recording: %u frames = %u seconds @ %u Hz", static_cast<unsigned>(TOTAL_FRAMES), static_cast<unsigned>(TEST_SECONDS), static_cast<unsigned>(SAMPLE_RATE));
    std::vector<int32_t> left_samples;
    std::vector<int32_t> right_samples;
    left_samples.reserve(TOTAL_FRAMES);
    right_samples.reserve(TOTAL_FRAMES);
    ESP_LOGW(TAG, "=== NOW RECORDING ===");
    vTaskDelay(pdMS_TO_TICKS(50));

    const int64_t record_start = esp_timer_get_time();
    while (left_samples.size() < TOTAL_FRAMES)
    {
        const esp_err_t err = sound.listen(left_samples, right_samples);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Recording failed: %s", esp_err_to_name(err));
            return;
        }
    }
    const int64_t record_end = esp_timer_get_time();
    // Keep exactly the requested number of frames.
    left_samples.resize(TOTAL_FRAMES);
    right_samples.resize(TOTAL_FRAMES);
    ESP_LOGW(TAG, "Recording wall time = %.3f seconds", (record_end - record_start) / 1000000.0);
    ESP_LOGW(TAG, "=== RECORDING COMPLETE: %u frames ===", static_cast<unsigned>(TOTAL_FRAMES));
    // ESP_LOGI(TAG, "===== RECORDED DATA =====");
    const size_t frames_to_debug = std::min(DEBUG_FRAMES, TOTAL_FRAMES);

    constexpr size_t BYTES_PER_SAMPLE = 3;
    constexpr size_t BYTES_PER_FRAME = 2 * BYTES_PER_SAMPLE;

    // std::vector<uint8_t> stereo_buffer(TOTAL_FRAMES * 2 * 3);
    std::vector<int32_t> stereo_buffer(TOTAL_FRAMES * 2);

    for (size_t i = 0; i < TOTAL_FRAMES; ++i)
    {
        stereo_buffer[(i * 2) + 0] = left_samples[i];
        stereo_buffer[(i * 2) + 1] = right_samples[i];
    }

    ESP_LOGW(TAG, "=== NOW PLAYING: %u frames ===", static_cast<unsigned>(TOTAL_FRAMES));

    sound.enable_speakers();

    sound.silence(2000);

    const int64_t playback_start = esp_timer_get_time();
    const esp_err_t err = sound.speak(stereo_buffer.data(), stereo_buffer.size(), 1024);

    const int64_t playback_end = esp_timer_get_time();
    sound.disable_speakers();

    left_samples.clear();
    right_samples.clear();
    stereo_buffer.clear();
}

extern "C" void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI("MAIN", "WIFI:SUCCESS");

    esp_err_t display_err = display.init();
    if (display_err != ESP_OK)
    {
        ESP_LOGE("MAIN", "Display initialization failed: %s", esp_err_to_name(display_err));
        return;
    }
    else
    {
        ESP_LOGI("MAIN", "DISPLAY:SUCCESS");
    }

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
    {
        ESP_LOGI("MAIN", "MOTOR:SUCCESS");
    }

    esp_err_t camera_err =
        camera.init();

    if (camera_err != ESP_OK)
    {
        ESP_LOGE(
            "MAIN",
            "Camera initialization failed: %s",
            esp_err_to_name(camera_err));

        return;
    }
    {
        ESP_LOGI("MAIN", "CAMERA:SUCCESS");
    }

    esp_err_t sound_err =
        sound.init();

    if (sound_err != ESP_OK)
    {
        ESP_LOGE(
            "MAIN",
            "Sound initialization failed: %s",
            esp_err_to_name(sound_err));

        return;
    }
    {
        ESP_LOGI("MAIN", "SOUND:SUCCESS");
    }

    //    vTaskDelay(pdMS_TO_TICKS(1000));
    // esp_err_t wifi_err = wifi_manager_init();

    // if (wifi_err != ESP_OK)
    // {
    //     ESP_LOGE(
    //         "MAIN",
    //         "Wi-Fi initialization failed: %s",
    //         esp_err_to_name(wifi_err));

    //     return;
    // }

    // esp_err_t ota_err =
    //     ota_manager_start();

    // if (ota_err != ESP_OK)
    // {
    //     ESP_LOGE(
    //         "MAIN",
    //         "OTA manager initialization failed: %s",
    //         esp_err_to_name(ota_err));

    //     return;
    // }

    // ESP_LOGI(
    //     "MAIN",
    //     "OTA:SUCCESS");

    if (camera.startTask() != ESP_OK)
    {
        ESP_LOGE(
            "MAIN",
            "Failed to start camera RX task");

        return;
    }

    ESP_LOGI(
    "MAIN",
    "Free heap: %lu",
    static_cast<unsigned long>(esp_get_free_heap_size()));

ESP_LOGI(
    "MAIN",
    "Free internal heap: %lu",
    static_cast<unsigned long>(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));

ESP_LOGI(
    "MAIN",
    "Free PSRAM: %lu",
    static_cast<unsigned long>(
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    const BaseType_t encoder_task_result =
        //  xTaskCreatePinnedToCoreWithCaps(
        xTaskCreatePinnedToCore(
        encoder_track_task,
        "encoder_task",
        4096,
        nullptr,
        6,
        nullptr,
        1
        // MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    // if (encoder_task_result != pdPASS)
    // {
    //     ESP_LOGE(
    //         "ENCODER",
    //         "Failed to create encoder task");

    //     return;
    // }

    if (encoder_task_result != pdPASS)
{
    ESP_LOGE(
        "ENCODER",
        "Failed to create encoder task: result=%d",
        static_cast<int>(encoder_task_result));

    ESP_LOGE(
        "ENCODER",
        "Free heap=%lu, minimum free heap=%lu",
        static_cast<unsigned long>(esp_get_free_heap_size()),
        static_cast<unsigned long>(esp_get_minimum_free_heap_size()));

    return;
}

    ESP_LOGI(
    "MAIN",
    "Encoder task created successfully");

    // Paint initial resting background layout face looking forward
    // display.draw_default_eyes(0, 0);

    const BaseType_t camera_display_task_result =
        // xTaskCreatePinnedToCoreWithCaps(
        xTaskCreatePinnedToCore(
        camera_display_task,
        "camera_task",
        6144,
        nullptr,
        4,
        nullptr,
        1
        // MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

  if (camera_display_task_result != pdPASS)
{
    ESP_LOGE(
        "CAMERA",
        "Failed to create camera display task: result=%d",
        static_cast<int>(camera_display_task_result));

    ESP_LOGE(
        "CAMERA",
        "Free heap=%lu, minimum free heap=%lu",
        static_cast<unsigned long>(esp_get_free_heap_size()),
        static_cast<unsigned long>(esp_get_minimum_free_heap_size()));

    return;
}

    ESP_LOGI(
    "MAIN",
    "Camera display task created successfully");

    // Pause briefly to let voltages stabilize before launching tracking routines

    while (true)
    {
        // test_speaker(sound);
        // test_record_playback_loop(sound);
        // vTaskDelay(pdMS_TO_TICKS(2000));

        // motor.move(200, 3000);
        vTaskDelay(pdMS_TO_TICKS(1000));
        // motor.spin(200, 3000);
        // vTaskDelay(pdMS_TO_TICKS(2000));
        // motor.turn_left(200, 3000);
        // vTaskDelay(pdMS_TO_TICKS(2000));
        // motor.turn_right(200, 3000);
        // vTaskDelay(pdMS_TO_TICKS(2000));
        // motor.reverse(200, 3000);
        // vTaskDelay(pdMS_TO_TICKS(2000));
        // motor.spin(200,5000);
        //  motor.spin(300, 5000);
    }
}
