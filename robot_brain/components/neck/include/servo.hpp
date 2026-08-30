#pragma once

#include "driver/ledc.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class Servo {
public:
    enum class Direction {
        CLOCKWISE,
        COUNTER_CLOCKWISE
    };

    Servo();
    ~Servo();

    // Prevent resource copies
    Servo(const Servo&) = delete;
    Servo& operator=(const Servo&) = delete;


    esp_err_t init(int gpio_num, ledc_mode_t speed_mode = LEDC_LOW_SPEED_MODE, ledc_channel_t channel = LEDC_CHANNEL_0);

    void set_angle(int target_angle, int duration_ms = 0, Direction dir = Direction::CLOCKWISE);

    void set_angle_async(int target_angle, int duration_ms = 0, Direction dir = Direction::CLOCKWISE);

    void middle();

    void reset();

private:

    uint32_t angle_to_duty(int angle);
    void stop_current_task(); // Kill active threads

    struct AsyncArgs { // pass multiple params into the freertos c-style task
        Servo* instance;
        int target_angle;
        int duration_ms;
        Direction dir;
    };

    static void async_move_task(void * pvParameters); // FreeRTOS worker

    ledc_mode_t speed_mode_;
    ledc_channel_t channel_;
    int current_angle_;
    bool initialized_;


    TaskHandle_t async_task_handle_{nullptr}; // track BG  movements
};
