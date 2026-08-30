#include "servo.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <cmath>

static const char* TAG = "ServoCtrl";

// 14-bit timer resolution (2^14 = 16384 ticks per 20ms)
// 0.5ms pulse = (0.5 / 20) * 16384 = 409.6 -> 410 ticks
// 2.5ms pulse = (2.5 / 20) * 16384 = 2048 ticks
constexpr uint32_t MIN_DUTY = 410;   
constexpr uint32_t MAX_DUTY = 2048;  

Servo::Servo() 
    : speed_mode_(LEDC_LOW_SPEED_MODE), 
      channel_(LEDC_CHANNEL_0), 
      current_angle_(90), 
      initialized_(false) {}

Servo::~Servo() {
    if (initialized_) {
        ledc_stop(speed_mode_, channel_, 0);
    }
}

esp_err_t Servo::init(int gpio_num, ledc_mode_t speed_mode, ledc_channel_t channel) {
    speed_mode_ = speed_mode;
    channel_ = channel;

    ledc_timer_config_t timer_conf{};
    timer_conf.speed_mode       = speed_mode_;
    timer_conf.duty_resolution  = LEDC_TIMER_14_BIT; 
    timer_conf.timer_num        = LEDC_TIMER_0;
    timer_conf.freq_hz          = 50;                
    timer_conf.clk_cfg          = LEDC_AUTO_CLK;

    esp_err_t err = ledc_timer_config(&timer_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LEDC Timer configuration");
        return err;
    }

    ledc_channel_config_t chan_conf{};
    chan_conf.gpio_num       = gpio_num;
    chan_conf.speed_mode     = speed_mode_;
    chan_conf.channel        = channel_;
    chan_conf.timer_sel      = LEDC_TIMER_0;
    chan_conf.duty           = angle_to_duty(current_angle_); 
    chan_conf.hpoint         = 0;
    
    err = ledc_channel_config(&chan_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to bind GPIO %d to LEDC channel", gpio_num);
        return err;
    }
    
    initialized_ = true; 
    return ESP_OK;
}

uint32_t Servo::angle_to_duty(int angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    
    return MIN_DUTY + (((MAX_DUTY - MIN_DUTY) * angle) / 180);
}

void Servo::set_angle(int target_angle, int duration_ms, Direction dir) {
    if (!initialized_) return;

    if (target_angle < 0) target_angle = 0;
    if (target_angle > 180) target_angle = 180;

    int final_angle = target_angle;
    if (dir == Direction::COUNTER_CLOCKWISE) {
        final_angle = 180 - target_angle;
    }

    if (duration_ms <= 0) {
        current_angle_ = final_angle;
        ledc_set_duty(speed_mode_, channel_, angle_to_duty(current_angle_));
        ledc_update_duty(speed_mode_, channel_);
        return;
    }

    int angle_difference = final_angle - current_angle_;
    if (angle_difference == 0) return;

    constexpr int TIME_STEP_MS = 20;
    int total_steps = duration_ms / TIME_STEP_MS;
    if (total_steps < 1) total_steps = 1;

    uint32_t start_duty = angle_to_duty(current_angle_);
    uint32_t end_duty = angle_to_duty(final_angle);
    float duty_difference = static_cast<float>(end_duty) - static_cast<float>(start_duty);

    // S-Curve Tuning Parameters
    // Steepness (k) controls how fast it accelerates. 
    // A value of 6.0 to 10.0 creates a beautiful, natural head-turn feel.
    constexpr float k = 8.0f; 

    for (int step = 1; step <= total_steps; ++step) {
        float t = static_cast<float>(step) / total_steps;

        float x = k * (t - 0.5f);
        float sigmoid = 1.0f / (1.0f + expf(-x));

        float sigmoid_min = 1.0f / (1.0f + expf(k * 0.5f));      // Value at t=0
        float sigmoid_max = 1.0f / (1.0f + expf(-k * 0.5f));     // Value at t=1
        float smooth_t = (sigmoid - sigmoid_min) / (sigmoid_max - sigmoid_min);

        uint32_t intermediate_duty = static_cast<uint32_t>(start_duty + (duty_difference * smooth_t));
        
        ledc_set_duty(speed_mode_, channel_, intermediate_duty);
        ledc_update_duty(speed_mode_, channel_);
        
        vTaskDelay(pdMS_TO_TICKS(TIME_STEP_MS)); 
    }

    // Lock in final position safely
    current_angle_ = final_angle;
    ledc_set_duty(speed_mode_, channel_, angle_to_duty(current_angle_));
    ledc_update_duty(speed_mode_, channel_);
    // ledc_stop(speed_mode_, channel_, 0);
}

void Servo::middle() { this->set_angle(90, 0); }
void Servo::reset() { this->set_angle(0, 0); }

void Servo::stop_current_task(){
    if (async_task_handle_ != nullptr){
        vTaskDelete(async_task_handle_);
        async_task_handle_ = nullptr;
        ESP_LOGD(TAG, "Interrupted existing servo movement task.");
    }
}

void Servo::set_angle_async(int target_angle, int duration_ms, Direction dir){
    if (!initialized_) return;

    stop_current_task();

    if(duration_ms <=0){
        set_angle(target_angle, duration_ms, dir);
        return;
    }

    AsyncArgs* args = new AsyncArgs{this, target_angle, duration_ms, dir};

    BaseType_t xReturned = xTaskCreate(
        Servo::async_move_task,
        "servo_async_move",
        3062,
        static_cast<void*>(args),
        5,
        &async_task_handle_
    );

    if(xReturned != pdPASS){
        ESP_LOGE(TAG, "Failed to create async task!");
        delete args;
    }
}

void Servo::async_move_task(void* pvParameters){
    AsyncArgs* args = static_cast<AsyncArgs*>(pvParameters);

    args->instance->set_angle(args->target_angle, args->duration_ms, args->dir);

    Servo* servo = args->instance;
    delete args;

    servo->async_task_handle_ = nullptr;
    vTaskDelete(NULL);
}