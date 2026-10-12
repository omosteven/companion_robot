#pragma once

#include <cstdint>
#include <vector>

#include "driver/gpio.h"

#include "esp_err.h"
#include "driver/i2s_std.h"



class Sound
{
public:
    Sound();
    ~Sound();

    Sound(const Sound &) = delete;
    Sound &operator=(const Sound &) = delete;

    enum class MicrophoneSide
    {
        LEFT,
        RIGHT
    };

    static constexpr gpio_num_t COMMON_MIC_WS_PIN = GPIO_NUM_8;
    static constexpr gpio_num_t COMMON_MIC_BCLK_PIN = GPIO_NUM_17;
    static constexpr gpio_num_t COMMON_MIC_SD_PIN = GPIO_NUM_18;

    static constexpr gpio_num_t COMMON_SPEAKER_WS_PIN = GPIO_NUM_8;
    static constexpr gpio_num_t COMMON_SPEAKER_BLCK_PIN = GPIO_NUM_17;
    static constexpr gpio_num_t COMMON_SPEAKER_SD_PIN = GPIO_NUM_21;

    static constexpr int SAMPLE_RATE = 48000;

    static constexpr int BUFFER_SIZE = 1024;

    esp_err_t init();
    esp_err_t listen(std::vector<int32_t> &left_samples, std::vector<int32_t> &right_samples);

    esp_err_t speak(const int32_t *samples,
                       size_t sample_count,
                       uint32_t buffer_size);
    esp_err_t enable_speakers();
    esp_err_t disable_speakers();

    esp_err_t enable_microphones();
    esp_err_t disable_microphones();

    esp_err_t silence(int32_t duration_ms);
    // void read_left();
    // void read_right();
    static constexpr size_t BUFFER_SAMPLES = BUFFER_SIZE;

private:
    // esp_err_t init_i2s(MicrophoneSide which_side);
    bool initialized_ = false;
    bool is_speaker_enabled = false;
    bool is_microphone_enabled = false;
    int32_t *read_buf = nullptr;

    i2s_chan_handle_t mic_rx_handle = nullptr;
    i2s_chan_handle_t speaker_tx_handle = nullptr;
};