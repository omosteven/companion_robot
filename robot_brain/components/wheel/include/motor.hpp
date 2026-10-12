#pragma once

#include <cstdint>
#include <cstddef>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/pulse_cnt.h"

class Motor
{
public:
    Motor();
    ~Motor();

    enum class MotorDirection
    {
        FORWARD,
        BACKWARD
    };

    struct MotorWheel
    {
        MotorDirection direction;
        int speed;
        int duration;
    };

    enum class Wheel
    {
        LEFT,
        RIGHT
    };

    // for queue-ing operations
    enum class MotorState { IDLE, RUNNING, COMPLETED, FAILED_STALL, FAILED_UNDERPERFORMED};
    enum class MovementType { STRAIGHT, TURN_LEFT, TURN_RIGHT, SPIN, REVERSE };
    struct MotorCommand {
        MovementType type;
        uint32_t speed; // rpm
        uint32_t duration_ms;
        float taregt_rpm_left;
        float target_rpm_right;

        uint32_t start_time_ms;
              uint32_t stored_to_queue_at_ms;
        MotorState state;
    };

    static constexpr QueueHandle_t motor_operations_queue = NULL;

    static constexpr int ENCODER_YELLOW_A_PIN = 16;
    static constexpr int ENCODER_GREEN_A_PIN = 15;

    static constexpr int ENCODER_YELLOW_B_PIN = 7;
    static constexpr int ENCODER_GREEN_B_PIN = 6;

    static constexpr int DRIVER_STBY_PIN = 8;
    static constexpr int DRIVER_PWM_A_PIN = 5;
    static constexpr int DRIVER_PWM_B_PIN = 4;

    static constexpr int DRIVER_AIN_1_PIN = 1;
    static constexpr int DRIVER_AIN_2_PIN = 2;

    static constexpr int DRIVER_BIN_1_PIN = 41;
    static constexpr int DRIVER_BIN_2_PIN = 42;

    static constexpr int MOTOR_MAX_SPEED = 620;

    esp_err_t encoder_init();
    esp_err_t driver_init();
    esp_err_t init();

    void turn_right(uint32_t speed = 100, uint32_t duration_ms = 0);
    void turn_left(uint32_t speed = 100, uint32_t duration_ms = 0);
    void reverse(uint32_t speed = 100, uint32_t duration_ms = 0);
    void spin(uint32_t speed = 100, uint32_t duration_ms = 0);
    void move(uint32_t speed = 100, uint32_t duration_ms = 0);
    void stop();

    void run_task(void *pvParameters);

    esp_err_t read_encoder(int *pulse_count_1, int *pulse_count_2);

    // void get_left_wheel_last_error();
    // void get_right_wheel_last_error();

    // void get_left_wheel_last_action();
    // void get_right_wheel_last_action();

    // void spin_wheel_forward();
    // void spin_wheel_backward();

    esp_err_t set_motor_speed(uint32_t speed, Wheel wheel);
    esp_err_t set_motor_direction(MotorDirection direction, Wheel wheel);
    esp_err_t send_to_queue(uint32_t left_speed, uint32_t right_speed, MovementType type, uint32_t duration_ms);

private:
    esp_err_t init_motor_pwm();

    esp_err_t set_motor_speed_channel(ledc_channel_t channel, uint32_t speed, uint32_t max_speed = 6000);

    bool initialized_motorA_ = false;
    bool initialized_motorB_ = false;
    bool initialized_encoder_ = false;
    bool initialized_driver_ = false;

    uint8_t left_wheel_last_error;
    uint8_t right_wheel_last_error;

    gpio_num_t stdby_gpio_;
    gpio_num_t ain1_gpio_;
    gpio_num_t ain2_gpio_;
    gpio_num_t bin1_gpio_;
    gpio_num_t bin2_gpio_;

    gpio_num_t encY1_gpio_;
    gpio_num_t encG1_gpio_;

    gpio_num_t encY2_gpio_;
    gpio_num_t encG2_gpio_;

    bool pwmA_gpio_initialized_;
    bool pwmB_gpio_initialized_;

    ledc_channel_t motor_A_pwm_channel_;
    ledc_channel_t motor_B_pwm_channel_;

    ledc_mode_t speed_mode_;

    pcnt_unit_handle_t encoder_1_pcnt_unit_;
    pcnt_unit_handle_t encoder_2_pcnt_unit_;

    void configure_encoder_pcnt_unit_(pcnt_unit_handle_t &pcnt_unit, Wheel wheel);
};