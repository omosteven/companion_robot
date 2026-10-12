#include "motor.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include <cstdint>
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>

#define MOTOR_FREQUENCY (20000)
#define MOTOR_RESOLUTION (LEDC_TIMER_10_BIT)

Motor::Motor()
    : speed_mode_(LEDC_LOW_SPEED_MODE) {

      };

Motor::~Motor() {
    // if (motor_A_pwm_channel_)
    // {
    //     ledc_stop(speed_mode_, motor_A_pwm_channel_, 0);
    // }

    // if (motor_B_pwm_channel_)
    // {
    //     ledc_stop(speed_mode_, motor_B_pwm_channel_, 0);
    // }
};

// {
//     start_time,
//     state: RUNNING/IDLE/COMPLETED/FAILED,
//     duration
//     target_rpm_left
//     taregt_rpm_right
//     wheel: LEFT/RIGHT/BOTH
//     end_time

// }
/// a cpu encoder track runs all time and only begins to keep record when it sees a queue with READY state
// then begins to count till its 'duration' completes and checks if total pulse converted to  rpm matches the target rpm

esp_err_t Motor::read_encoder(int *pulse_count_1, int *pulse_count_2)
{

    // Fetch the raw signed integer count directly from hardware memory registers
    pcnt_unit_get_count(encoder_1_pcnt_unit_, pulse_count_1);
    pcnt_unit_get_count(encoder_2_pcnt_unit_, pulse_count_2);

    // if count is low maybe due to too much frictions, force stop the wheels to avoid burn out
    // if (*pulse_count_1 <= 100 || *pulse_count_2 <= 100){
    //     this->stop();
    // }
    esp_err_t err = pcnt_unit_clear_count(encoder_1_pcnt_unit_);
    err = pcnt_unit_clear_count(encoder_2_pcnt_unit_);
    // ESP_LOGI("Encoders:", "Resetting to LEFT=%d RIGHT=%d", *pulse_count_1, *pulse_count_2);

    return err;
};

void Motor::configure_encoder_pcnt_unit_(pcnt_unit_handle_t &pcnt_unit, Wheel wheel)
{
    pcnt_unit_config_t unit_config = {};
    unit_config.low_limit = -32768;
    unit_config.high_limit = 32767;
    unit_config.flags = {.accum_count = true};

    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    pcnt_channel_handle_t pcnt_chan = NULL;
    pcnt_chan_config_t chan_config = {};
    chan_config.edge_gpio_num = wheel == Wheel::LEFT ? encY1_gpio_ : encY2_gpio_;
    chan_config.level_gpio_num = wheel == Wheel::LEFT ? encG1_gpio_ : encG2_gpio_;
    chan_config.flags = {};
    // chan_config.flags.io_loop_back = false;

    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_config, &pcnt_chan));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan,
                                                 PCNT_CHANNEL_EDGE_ACTION_INCREASE, // If Edge signal moves from  low to high, add 1 to the count
                                                 PCNT_CHANNEL_EDGE_ACTION_HOLD));   // if signal moves from high to low, do nothing

    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan,
                                                  PCNT_CHANNEL_LEVEL_ACTION_INVERSE, // When Control Pin is HIGH
                                                  PCNT_CHANNEL_LEVEL_ACTION_KEEP));  // When Control Pin is LOW

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));

    // pcnt_unit_clear(pcnt_unit);
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    ESP_LOGI("ENCODER", "PCNT Armed on", pcnt_unit);
}

esp_err_t Motor::encoder_init()
{
    encY1_gpio_ = static_cast<gpio_num_t>(ENCODER_YELLOW_A_PIN); // pulse input
    encG1_gpio_ = static_cast<gpio_num_t>(ENCODER_GREEN_A_PIN);  // control input

    encY2_gpio_ = static_cast<gpio_num_t>(ENCODER_YELLOW_B_PIN); // pulse input
    encG2_gpio_ = static_cast<gpio_num_t>(ENCODER_GREEN_B_PIN);

    gpio_set_pull_mode(encY1_gpio_, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(encG1_gpio_, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(encY2_gpio_, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(encG2_gpio_, GPIO_PULLUP_ONLY);

    encoder_1_pcnt_unit_ = NULL;
    encoder_2_pcnt_unit_ = NULL;

    this->configure_encoder_pcnt_unit_(encoder_1_pcnt_unit_, Wheel::LEFT);
    this->configure_encoder_pcnt_unit_(encoder_2_pcnt_unit_, Wheel::RIGHT);
    return ESP_OK;
}

esp_err_t Motor::init_motor_pwm()
{

    gpio_num_t pwmA_gpio_ = static_cast<gpio_num_t>(DRIVER_PWM_A_PIN);
    gpio_num_t pwmB_gpio_ = static_cast<gpio_num_t>(DRIVER_PWM_B_PIN);

    ledc_timer_config_t ledc_timer{};

    ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_timer.duty_resolution = MOTOR_RESOLUTION;
    ledc_timer.timer_num = LEDC_TIMER_1;
    ledc_timer.freq_hz = MOTOR_FREQUENCY;
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;

    esp_err_t timer_err = ledc_timer_config(&ledc_timer);

    if (timer_err != ESP_OK)
    {
        ESP_LOGE("MOTOR PWM", "Failed to initialize LEDC Timer configuration");
        return timer_err;
    }

    motor_A_pwm_channel_ = LEDC_CHANNEL_0;
    motor_B_pwm_channel_ = LEDC_CHANNEL_1;

    ledc_channel_config_t motor_A_pwm_channel_config{};

    motor_A_pwm_channel_config.gpio_num = pwmA_gpio_;
    motor_A_pwm_channel_config.speed_mode = LEDC_LOW_SPEED_MODE;
    motor_A_pwm_channel_config.channel = motor_A_pwm_channel_;
    // motor_A_pwm_channel_.intr_type = LEDC_INTR_DISABLE;
    motor_A_pwm_channel_config.timer_sel = LEDC_TIMER_1;
    motor_A_pwm_channel_config.duty = 0;
    motor_A_pwm_channel_config.hpoint = 0;

    ledc_channel_config_t motor_B_pwm_channel_config{};
    motor_B_pwm_channel_config.gpio_num = pwmB_gpio_;
    motor_B_pwm_channel_config.speed_mode = LEDC_LOW_SPEED_MODE;
    motor_B_pwm_channel_config.channel = motor_B_pwm_channel_;
    // motor_B_pwm_channel_.intr_type = LEDC_INTR_DISABLE;
    motor_B_pwm_channel_config.timer_sel = LEDC_TIMER_1;
    motor_B_pwm_channel_config.duty = 0;
    motor_B_pwm_channel_config.hpoint = 0;

    esp_err_t err = ledc_channel_config(&motor_A_pwm_channel_config);
    if (err != ESP_OK)
    {
        ESP_LOGE("MOTOR PWM", "Failed to initialize channel A PWM configuration");
        return err;
    }

    err = ledc_channel_config(&motor_B_pwm_channel_config);
    if (err != ESP_OK)
    {
        ESP_LOGE("MOTOR PWM", "Failed to initialize channel B PWM configuration");
        return err;
    }

    ESP_LOGI("MOTOR PWM", "PWM Configuration for A and B Successful");

    return ESP_OK;
};

esp_err_t Motor::driver_init()
{
    // stdby_gpio_ = static_cast<gpio_num_t>(DRIVER_STBY_PIN);

    ain1_gpio_ = static_cast<gpio_num_t>(DRIVER_AIN_1_PIN);
    ain2_gpio_ = static_cast<gpio_num_t>(DRIVER_AIN_2_PIN);
    bin1_gpio_ = static_cast<gpio_num_t>(DRIVER_BIN_1_PIN);
    bin2_gpio_ = static_cast<gpio_num_t>(DRIVER_BIN_2_PIN);

    // gpio_set_direction(stdby_gpio_, GPIO_MODE_OUTPUT);

    gpio_set_direction(ain1_gpio_, GPIO_MODE_OUTPUT);
    gpio_set_direction(ain2_gpio_, GPIO_MODE_OUTPUT);

    gpio_set_direction(bin1_gpio_, GPIO_MODE_OUTPUT);
    gpio_set_direction(bin2_gpio_, GPIO_MODE_OUTPUT);

    gpio_set_level(ain1_gpio_, 0);
    gpio_set_level(ain2_gpio_, 0);
    gpio_set_level(bin1_gpio_, 0);
    gpio_set_level(bin2_gpio_, 0);

    // gpio_set_level(stdby_gpio_, 1);

    // -- detect if gpios are connected or not ---
    // gpio_set_pull_mode(stdby_gpio_, GPIO_PULLUP_ONLY); // enable the driver

    vTaskDelay(pdMS_TO_TICKS(10));
    // if (gpio_get_level(stdby_gpio_) == 0)
    // {
    //     ESP_LOGE("MOTOR DRIVER", "Driver Not Detected or Not Physically connected!");
    //     return ESP_ERR_NOT_FOUND;
    // }
    // we assume the driver is awaken because it is impossible for the driver to communicate back its stdby state
    initialized_driver_ = true;
    ESP_LOGI("MOTOR DRIVER", "DETECTION successful!");
    return ESP_OK;
}

esp_err_t Motor::init()
{
    esp_err_t driver_err = this->driver_init();

    esp_err_t encoder_err = this->encoder_init();
    esp_err_t motor_pwm_err = this->init_motor_pwm();

    if ((driver_err != ESP_OK) | (encoder_err != ESP_OK) | (motor_pwm_err != ESP_OK))
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t Motor::set_motor_speed_channel(ledc_channel_t channel, uint32_t speed, uint32_t max_speed)
{
    // max duty = 100% = 6000rpm = 12v
    // 50% duty = 3000rpm/12v
    if (speed > max_speed)
        speed = max_speed;

    // uint32_t speed_percentage = speed / max_speed * 100;
    uint32_t max_duty = (1 << MOTOR_RESOLUTION) - 1; // total number of ticks/steps possible = 2^10 -1 = 1023

    uint32_t target_duty = (speed * max_duty) / max_speed;

    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, target_duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);

    return ESP_OK;
};

esp_err_t Motor::set_motor_speed(uint32_t speed, Wheel wheel)
{
    if (!initialized_driver_)
        return ESP_ERR_NOT_FOUND;

    this->set_motor_speed_channel(wheel == Wheel::LEFT ? motor_A_pwm_channel_ : motor_B_pwm_channel_, speed, MOTOR_MAX_SPEED);
    return ESP_OK;
}

esp_err_t Motor::set_motor_direction(MotorDirection direction, Wheel wheel)
{

    if (!initialized_driver_)
        return ESP_ERR_NOT_FOUND;
    gpio_set_level(
        wheel == Wheel::LEFT ? ain1_gpio_ : bin1_gpio_,
        direction == MotorDirection::FORWARD ? 1 : 0);

    gpio_set_level(
        wheel == Wheel::LEFT ? ain2_gpio_ : bin2_gpio_,
        direction == MotorDirection::FORWARD ? 0 : 1);

    return ESP_OK;
}

void Motor::turn_right(uint32_t speed, uint32_t duration_ms)
{

    if (speed == 0)
        return;

    uint32_t differential_speed = speed - 100;

    if (speed <= 0)
        differential_speed = speed;

    this->set_motor_direction(this->MotorDirection::FORWARD, this->Wheel::RIGHT);
    this->set_motor_direction(this->MotorDirection::FORWARD, this->Wheel::LEFT);

    this->set_motor_speed(differential_speed, this->Wheel::RIGHT);
    this->set_motor_speed(speed, this->Wheel::LEFT);

    // if (duration_ms > 0)
    // {
    //     vTaskDelay(
    //         pdMS_TO_TICKS(duration_ms));
    //     this->set_motor_speed(0, this->Wheel::LEFT);
    //     this->set_motor_speed(0, this->Wheel::RIGHT);
    // }
}

void Motor::turn_left(uint32_t speed, uint32_t duration_ms)
{
    if (speed == 0)
        return;

    uint32_t differential_speed = speed - 100;

    if (speed <= 0)
        differential_speed = speed;
    this->set_motor_direction(this->MotorDirection::FORWARD, this->Wheel::LEFT);
    this->set_motor_direction(this->MotorDirection::FORWARD, this->Wheel::RIGHT);

    this->set_motor_speed(differential_speed, this->Wheel::LEFT);
    this->set_motor_speed(speed, this->Wheel::RIGHT);

    // if (duration_ms > 0)
    // {
    //     vTaskDelay(
    //         pdMS_TO_TICKS(duration_ms));
    //     this->set_motor_speed(0, this->Wheel::LEFT);
    //     this->set_motor_speed(0, this->Wheel::RIGHT);
    // }
}

void Motor::reverse(uint32_t speed, uint32_t duration_ms)
{
    if (speed == 0)
        return;
    this->set_motor_direction(this->MotorDirection::BACKWARD, this->Wheel::LEFT);
    this->set_motor_direction(this->MotorDirection::BACKWARD, this->Wheel::RIGHT);

    this->set_motor_speed(speed, this->Wheel::RIGHT);
    this->set_motor_speed(speed, this->Wheel::LEFT);

    // if (duration_ms > 0)
    // {
    //     vTaskDelay(
    //         pdMS_TO_TICKS(duration_ms));
    //     this->set_motor_speed(0, this->Wheel::LEFT);
    //     this->set_motor_speed(0, this->Wheel::RIGHT);
    // }
}
void Motor::spin(uint32_t speed, uint32_t duration_ms)
{
    if (speed == 0)
        return;
    this->set_motor_direction(this->MotorDirection::FORWARD, this->Wheel::LEFT);
    this->set_motor_direction(this->MotorDirection::BACKWARD, this->Wheel::RIGHT);

    this->set_motor_speed(speed, this->Wheel::RIGHT);
    this->set_motor_speed(speed, this->Wheel::LEFT);

    // if (duration_ms > 0)
    // {
    //     vTaskDelay(
    //         pdMS_TO_TICKS(duration_ms));
    //     this->set_motor_speed(0, this->Wheel::LEFT);
    //     this->set_motor_speed(0, this->Wheel::RIGHT);
    // }
}

void Motor::move(uint32_t speed, uint32_t duration_ms)
{
    if (speed == 0)
        return;
    this->set_motor_direction(this->MotorDirection::FORWARD, this->Wheel::LEFT);
    this->set_motor_direction(this->MotorDirection::FORWARD, this->Wheel::RIGHT);

    this->set_motor_speed(speed, this->Wheel::LEFT);
    this->set_motor_speed(speed, this->Wheel::RIGHT);

    // if (duration_ms > 0)
    // {
    //     vTaskDelay(
    //         pdMS_TO_TICKS(duration_ms));
    //     this->set_motor_speed(0, this->Wheel::LEFT);
    //     this->set_motor_speed(0, this->Wheel::RIGHT);
    // }
}

void Motor::stop()
{
    if (!initialized_driver_)
        return;

    this->set_motor_speed(0, this->Wheel::LEFT);
    this->set_motor_speed(0, this->Wheel::RIGHT);
}

esp_err_t Motor::send_to_queue(uint32_t left_speed, uint32_t right_speed, Motor::MovementType type, uint32_t duration_ms)
{

    MotorCommand operation{};

    operation.type = type;
    operation.taregt_rpm_left = left_speed;
    operation.target_rpm_right = right_speed;
    operation.state = this->MotorState::IDLE;
    operation.stored_to_queue_at_ms = esp_timer_get_time();

    if (xQueueSend(this->motor_operations_queue, &operation, 0) != pdPASS)
    {
        ESP_LOGW("MOTOR", "Operation Queue Full! Dropping this operation");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void Motor::run_task(void *pvParameters)
{

    int left_pulses = 0, right_pulses = 0;
    const uint32_t CONTROL_INTERVAL_MS = 100;
    
    while (true)
    {
        MotorCommand current_operation;

        if (xQueueReceive(motor_operations_queue, &current_operation, portMAX_DELAY) == pdPASS)
        {
            ESP_LOGI("MOTOR_QUEUE", "Executing command type: %d for %lu ms", (int)current_operation.type, current_operation.duration_ms);

            switch (current_operation.type)
            {
            case this->MovementType::STRAIGHT:
                this->move(current_operation.speed, current_operation.duration_ms);
                /* code */
                break;

            case this->MovementType::TURN_LEFT:
                this->turn_left(current_operation.speed, current_operation.duration_ms);
                break;

            case this->MovementType::TURN_RIGHT:
                this->turn_right(current_operation.speed, current_operation.duration_ms);
                break;

            case this->MovementType::SPIN:
                this->spin(current_operation.speed, current_operation.duration_ms);
                break;

            case this->MovementType::REVERSE:
                this->reverse(current_operation.speed, current_operation.duration_ms);
                break;

            default:
                break;
            }

            this->read_encoder(&left_pulses, &right_pulses);

            current_operation.start_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            current_operation.state = MotorState::RUNNING;

            uint32_t elapsed_time_ms = 0;

            while (elapsed_time_ms < current_operation.duration_ms)
            {

                vTaskDelay(pdMS_TO_TICKS(CONTROL_INTERVAL_MS));
                elapsed_time_ms = (xTaskGetTickCount() * portTICK_PERIOD_MS) - current_operation.start_time_ms;

                this->read_encoder(&left_pulses, &right_pulses);
            }

            this->stop();

            ESP_LOGI("MOTOR QUEUE", "COMPLETED total left pulses=%d total right pulse=%d", left_pulses, right_pulses);
        }
    }
}