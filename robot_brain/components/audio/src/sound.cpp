#include "sound.hpp"

#include <cstdlib>
#include <cmath>

#include "driver/gpio.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <bitset>
#include <algorithm>
#include <cinttypes>

#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "soc/i2s_struct.h"

static const char *TAG = "SOUND";

Sound::Sound()
{
    read_buf = static_cast<int32_t *>(calloc(BUFFER_SAMPLES, sizeof(int32_t)));
};

Sound::~Sound()
{
    if (mic_rx_handle)
    {
        if (is_microphone_enabled)
        {
            i2s_channel_disable(mic_rx_handle);
            is_microphone_enabled = false;
        }
        i2s_del_channel(mic_rx_handle);
        mic_rx_handle = nullptr;
    }

    if (speaker_tx_handle)
    {
        if (is_speaker_enabled)
        {
            i2s_channel_disable(speaker_tx_handle);
        }
        i2s_del_channel(speaker_tx_handle);
        speaker_tx_handle = nullptr;
    }

    if (read_buf)
    {
        free(read_buf);
        read_buf = nullptr;
    }

    initialized_ = false;
};

esp_err_t Sound::init()
{
    if (initialized_)
    {
        ESP_LOGW(TAG, "Sound already initialized");
        return ESP_OK;
    }

    if (!read_buf)
    {
        ESP_LOGE(
            TAG,
            "Failed to allocate microphone buffer");

        return ESP_ERR_NO_MEM;
    }

    esp_err_t err;

    i2s_chan_config_t mic_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    err = i2s_new_channel(&mic_chan_cfg, nullptr, &mic_rx_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to create Microphone I2S RX channel: %s",
            esp_err_to_name(err));

        return err;
    }

    i2s_std_config_t mic_cfg = {};
    mic_cfg.clk_cfg.sample_rate_hz = SAMPLE_RATE;
    mic_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    mic_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384; // Safe for 24-bit
    mic_cfg.slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT,
            I2S_SLOT_MODE_STEREO);
    mic_cfg.slot_cfg.slot_bit_width =
        I2S_SLOT_BIT_WIDTH_32BIT;
    mic_cfg.slot_cfg.ws_width = 32;

    mic_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    mic_cfg.gpio_cfg.bclk = COMMON_MIC_BCLK_PIN;
    mic_cfg.gpio_cfg.ws = COMMON_MIC_WS_PIN;
    mic_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
    mic_cfg.gpio_cfg.din = COMMON_MIC_SD_PIN;
    mic_cfg.gpio_cfg.invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,
    };

    err = i2s_channel_init_std_mode(mic_rx_handle, &mic_cfg);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to initialize I2S Mic: %s",
            esp_err_to_name(err));
        i2s_del_channel(mic_rx_handle);
        mic_rx_handle = nullptr;
        return err;
    }

    // FOR SPEAKER
    i2s_chan_config_t speaker_chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(
            I2S_NUM_1,
            I2S_ROLE_SLAVE);

    err = i2s_new_channel(
        &speaker_chan_cfg,
        &speaker_tx_handle,
        nullptr);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to create speaker I2S channel: %s",
            esp_err_to_name(err));

        i2s_del_channel(speaker_tx_handle);
        speaker_tx_handle = nullptr;

        return err;
    }

    i2s_std_config_t speaker_cfg = {};
    speaker_cfg.clk_cfg.sample_rate_hz =
        SAMPLE_RATE;

    speaker_cfg.clk_cfg.clk_src =
        I2S_CLK_SRC_DEFAULT;

    speaker_cfg.clk_cfg.mclk_multiple =
        I2S_MCLK_MULTIPLE_384;
    speaker_cfg.slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT,
            I2S_SLOT_MODE_STEREO);

    speaker_cfg.slot_cfg.slot_bit_width =
        I2S_SLOT_BIT_WIDTH_32BIT;

    speaker_cfg.slot_cfg.ws_width =
        32;

        // speaker_cfg.slot_cfg.left_align =false;
    speaker_cfg.gpio_cfg.mclk =
        I2S_GPIO_UNUSED;

    speaker_cfg.gpio_cfg.bclk =
        COMMON_SPEAKER_BLCK_PIN;

    speaker_cfg.gpio_cfg.ws = COMMON_SPEAKER_WS_PIN;

    speaker_cfg.gpio_cfg.dout =
        COMMON_SPEAKER_SD_PIN;

    speaker_cfg.gpio_cfg.din =
        I2S_GPIO_UNUSED;

    speaker_cfg.gpio_cfg.invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,
    };

    err = i2s_channel_init_std_mode(
        speaker_tx_handle,
        &speaker_cfg);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to Initialized speaker I2S: %s",
            esp_err_to_name(err));
        i2s_del_channel(speaker_tx_handle);
        speaker_tx_handle = nullptr;

        return err;
    }

    err =
        i2s_channel_enable(
            mic_rx_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to enable microphones: %s",
            esp_err_to_name(err));

        return err;
    }

    is_microphone_enabled = true;
    initialized_ = true;

    ESP_LOGI(
        TAG,
        "Sound initialized successfully");

    ESP_LOGI(
        TAG,
        "MIC  : I2S0 RX @ 48 kHz, 24-bit data, 32-bit slots");

    ESP_LOGI(
        TAG,
        "SPEAKER: I2S1 TX @ 48 kHz, 24-bit data, 32-bit slots");

    return ESP_OK;
}

esp_err_t Sound::listen(
    std::vector<int32_t> &left_samples,
    std::vector<int32_t> &right_samples)
{
    if (!initialized_ || !is_microphone_enabled)
    {
        ESP_LOGE(
            TAG,
            "MIC Not Initialied");
        return ESP_ERR_INVALID_STATE;
    }

    size_t bytes_read = 0;

    esp_err_t ret = i2s_channel_read(
        mic_rx_handle,
        read_buf,
        BUFFER_SIZE * sizeof(int32_t),
        &bytes_read,
        portMAX_DELAY);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "I2S Read Failed: %s",
            esp_err_to_name(ret));

        return ret;
    }

    const size_t total_samples =
        bytes_read / sizeof(int32_t);

    constexpr size_t DEBUG_FRAMES = 8;
    const size_t frames_read = total_samples / 2;
    const size_t frames_to_log = std::min(frames_read, DEBUG_FRAMES);

    // [L][R][L][R][L][R]...
    for (size_t i = 0; i + 1 < total_samples; i += 2)
    {
        const int32_t left_sample =
            static_cast<int32_t>(read_buf[i]);

        const int32_t right_sample =
            static_cast<int32_t>(read_buf[i + 1]);

        left_samples.push_back(left_sample);
        right_samples.push_back(right_sample);
    }

    return ESP_OK;
}

esp_err_t Sound::enable_speakers()
{
    if (!initialized_ ||
        speaker_tx_handle == nullptr)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (is_speaker_enabled)
    {
        return ESP_OK;
    }

    esp_err_t err =
        i2s_channel_enable(
            speaker_tx_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to enable speaker: %s",
            esp_err_to_name(err));

        return err;
    }

    is_speaker_enabled = true;

    ESP_LOGI(
        TAG,
        "Speaker I2S enabled");
    return ESP_OK;
};

esp_err_t Sound::disable_speakers()
{
    if (!initialized_ ||
        speaker_tx_handle == nullptr)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (!is_speaker_enabled)
    {
        return ESP_OK;
    }

    esp_err_t err =
        i2s_channel_disable(
            speaker_tx_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to disable speaker: %s",
            esp_err_to_name(err));

        return err;
    }

    is_speaker_enabled = false;

    ESP_LOGI(
        TAG,
        "Speaker I2S disabled");

    return ESP_OK;
};

esp_err_t Sound::enable_microphones()
{
    if (!initialized_ ||
        mic_rx_handle == nullptr)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (is_microphone_enabled)
    {
        return ESP_OK;
    }

    esp_err_t err =
        i2s_channel_enable(
            mic_rx_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to enable microphones: %s",
            esp_err_to_name(err));

        return err;
    }

    is_microphone_enabled = true;

    ESP_LOGI(
        TAG,
        "Microphone I2S enabled");

    return ESP_OK;
};

esp_err_t Sound::disable_microphones()
{
    if (!initialized_ ||
        mic_rx_handle == nullptr)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (!is_microphone_enabled)
    {
        return ESP_OK;
    }

    esp_err_t err =
        i2s_channel_disable(
            mic_rx_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to disable microphones: %s",
            esp_err_to_name(err));

        return err;
    }

    is_microphone_enabled = false;

    ESP_LOGI(
        TAG,
        "Microphone I2S disabled");

    return ESP_OK;
};

static inline int32_t apply_gain(
    int32_t sample,
    float gain)
{
    if (gain == 1.0f)
        return sample;

    constexpr int64_t MAX_24_SLOT =
        static_cast<int64_t>(8388607) << 8;

    constexpr int64_t MIN_24_SLOT =
        static_cast<int64_t>(-8388608) << 8;

    const int64_t amplified =
        static_cast<int64_t>(
            std::llround(
                static_cast<double>(sample) *
                static_cast<double>(gain)));

    if (amplified > MAX_24_SLOT)
        return static_cast<int32_t>(MAX_24_SLOT);

    if (amplified < MIN_24_SLOT)
        return static_cast<int32_t>(MIN_24_SLOT);

    return static_cast<int32_t>(amplified);
}

esp_err_t Sound::speak(const int32_t *samples,
                       size_t sample_count,
                       uint32_t buffer_size)
{

    if (!initialized_ || !is_speaker_enabled)
        return ESP_ERR_INVALID_STATE;

    if (samples == nullptr || sample_count == 0 || buffer_size == 0)
        return ESP_ERR_INVALID_ARG;

    constexpr size_t FRAMES_PER_CHUNK = 512;
    constexpr size_t SAMPLES_PER_FRAME = 2;
    std::vector<int32_t> tx_buffer(FRAMES_PER_CHUNK * SAMPLES_PER_FRAME);
    size_t samples_sent = 0;
    constexpr float GAIN = 1.25f;

    while (samples_sent < sample_count)
    {
        const size_t samples_remaining = sample_count - samples_sent;
        const size_t samples_to_process = std::min(samples_remaining, tx_buffer.size()); /* * Apply gain independently to every * 32-bit L/R slot. */
        for (size_t i = 0; i < samples_to_process; ++i)
        {
            // tx_buffer[i] = apply_gain(samples[samples_sent + i], GAIN)>>8;
            tx_buffer[i] = samples[samples_sent + i];

        }
        const uint8_t *data = reinterpret_cast<const uint8_t *>(tx_buffer.data());
        const size_t byte_count = samples_to_process * sizeof(int32_t);
        size_t bytes_sent = 0;
        while (bytes_sent < byte_count)
        {
            const size_t bytes_remaining = byte_count - bytes_sent;
            const size_t bytes_to_send = std::min(bytes_remaining, static_cast<size_t>(buffer_size));
            size_t bytes_written = 0;
            esp_err_t err = i2s_channel_write(speaker_tx_handle, data + bytes_sent, bytes_to_send, &bytes_written, portMAX_DELAY);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "I2S TX failed: %s", esp_err_to_name(err));
                return err;
            }
            if (bytes_written == 0)
            {
                ESP_LOGE(TAG, "I2S TX stalled: written 0 bytes");
                return ESP_FAIL;
            }
            bytes_sent += bytes_written;
        }
        samples_sent += samples_to_process;
    }
    return ESP_OK;
}

esp_err_t Sound::silence(int32_t duration_ms)
{
    if (!initialized_ || !is_speaker_enabled)
        return ESP_ERR_INVALID_STATE;

    if (duration_ms <= 0)
        return ESP_ERR_INVALID_ARG;

    constexpr size_t FRAMES_PER_CHUNK = 512;

    const size_t total_frames =
        (static_cast<size_t>(duration_ms) * SAMPLE_RATE) / 1000;

    std::vector<int32_t> silence(
        FRAMES_PER_CHUNK * 2,
        0);

    size_t frames_sent = 0;

    while (frames_sent < total_frames)
    {
        const size_t remaining_frames =
            total_frames - frames_sent;

        const size_t frames =
            std::min(
                remaining_frames,
                FRAMES_PER_CHUNK);

        const size_t sample_count =
            frames * 2;

        esp_err_t err =
            this->speak(
                silence.data(),
                sample_count,
                1024);

        if (err != ESP_OK)
            return err;

        frames_sent += frames;
    }

    return ESP_OK;
}