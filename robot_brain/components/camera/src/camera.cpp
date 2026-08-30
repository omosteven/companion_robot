#include "camera.hpp"

#include "driver/gpio.h"

#include "esp_log.h"
#include "esp_heap_caps.h"

#include <cstdlib>
#include <cstring>
#include "esp_timer.h"
#include <inttypes.h>

static const char *TAG = "Camera";

Camera::Camera()
    : m_bufferMutex(nullptr),
      m_taskHandle(nullptr),
      m_isInitialized(false),
      m_frameCounter(0)
{
    for (auto &buffer : m_buffers)
    {
        buffer.data = nullptr;
        buffer.size = 0;
        buffer.frame_number = 0;
        buffer.state = BufferState::FREE;
    }
}

Camera::~Camera()
{
    if (m_taskHandle)
    {
        vTaskDelete(m_taskHandle);
        m_taskHandle = nullptr;
    }

    if (m_bufferMutex)
    {
        vSemaphoreDelete(m_bufferMutex);
        m_bufferMutex = nullptr;
    }

    for (auto &buffer : m_buffers)
    {
        if (buffer.data)
        {
            free(buffer.data);
            buffer.data = nullptr;
        }
    }

    uart_driver_delete(UART_PORT);
}

esp_err_t Camera::init()
{

       gpio_num_t rx_gpio = static_cast<gpio_num_t>(RX_PIN);
        gpio_reset_pin(rx_gpio);
        gpio_set_direction(rx_gpio, GPIO_MODE_INPUT);
        gpio_set_pull_mode(rx_gpio, GPIO_PULLUP_ONLY); // Force it HIGH if empty
        vTaskDelay(pdMS_TO_TICKS(10));                                           // 2. Read the physical pin state
        if (gpio_get_level(rx_gpio) == 0)
        {
            ESP_LOGE("CameraCheck", "❌ DETECT: Camera board is NOT physically connected or lacks power!");
            return ESP_ERR_NOT_FOUND;
        }
        ESP_LOGI("CameraCheck", "DETECT: Camera board connection detected on pin %d!", RX_PIN);
    
        /*
     * Allocate triple JPEG buffers.
     *
     * 3 × 128 KB 
     *
     * PSRAM is preferred.
     * This will be used to store frames in three different states
     * We get every frame and keep in a buffer A with replacement
     * We get frame from buffer A, decode and keep in B
     * We get frame from buffer B, dispatch to screen and in keep in C
     */
    for (int i = 0; i < BUFFER_COUNT; ++i)
    {
        m_buffers[i].data =
            static_cast<uint8_t *>(
                heap_caps_malloc(
                    MAX_JPEG_SIZE,
                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

        if (!m_buffers[i].data)
        {
            ESP_LOGE(
                TAG,
                "Failed to allocate JPEG buffer %d",
                i);

            return ESP_ERR_NO_MEM;
        }

        m_buffers[i].size = 0;
        m_buffers[i].frame_number = 0;
        m_buffers[i].state = BufferState::FREE;
    }

    m_bufferMutex = xSemaphoreCreateMutex();

    if (!m_bufferMutex)
    {
        return ESP_ERR_NO_MEM;
    }

    uart_config_t uart_config = {};

    uart_config.baud_rate = BAUD_RATE;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(
        uart_driver_install(
            UART_PORT,
            RX_BUF_SIZE,
            0,
            0,
            nullptr,
            0));

    ESP_ERROR_CHECK(
        uart_param_config(
            UART_PORT,
            &uart_config));

    ESP_ERROR_CHECK(
        uart_set_pin(
            UART_PORT,
            TX_PIN,
            RX_PIN,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE));

    uart_flush_input(UART_PORT);
    m_isInitialized = true;

    ESP_LOGI(
        TAG,
        "UART camera receiver initialized: %d baud",
        BAUD_RATE);

    return ESP_OK;
}

esp_err_t Camera::startTask()
{
    if (!m_isInitialized)
    {
        return ESP_FAIL;
    }

    BaseType_t result =
        xTaskCreatePinnedToCore(
            Camera::rxTaskTrampoline,
            "cam_rx_loop",
            4096,
            this,
            8,
            &m_taskHandle,
            0);

    return (result == pdPASS)
               ? ESP_OK
               : ESP_FAIL;
}

void Camera::rxTaskTrampoline(void *pvParameters)
{
    static_cast<Camera *>(pvParameters)->executionLoop();
}

bool Camera::readExact(
    uint8_t *buffer,
    size_t length)
{
    // to read the size of incoming frame
    size_t received = 0;

    while (received < length)
    {
        int ret =
            uart_read_bytes(
                UART_PORT,
                buffer + received,
                length - received,
                portMAX_DELAY);

        // ESP_LOGI("READ EXACT", "Seen? %d len =%d", buffer + received, ret);
        if (ret <= 0)
        {
            return false;
        }

        received += static_cast<size_t>(ret);
    }

    return true;
}

// for finding frame header
bool Camera::findSync()
{
    static constexpr uint8_t SYNC[4] =
        {
            0xAA,
            0xBB,
            0xCC,
            0xDD};

    uint8_t byte = 0;

    int sync_index = 0;

    while (true)
    {
        int ret =
            uart_read_bytes(
                UART_PORT,
                &byte,
                1,
                portMAX_DELAY);

        if (ret != 1)
        {
            continue;
        }

        if (byte == SYNC[sync_index])
        {
            sync_index++;

            if (sync_index == 4)
            {
                return true;
            }
        }
        else
        {
            sync_index =
                (byte == SYNC[0]) ? 1 : 0;
        }
    }
}

Camera::FrameBuffer *Camera::getFreeBuffer()
{
    FrameBuffer *result = nullptr;

    xSemaphoreTake(
        m_bufferMutex,
        portMAX_DELAY);

    for (auto &buffer : m_buffers)
    {
        if (buffer.state == BufferState::FREE)
        {
            buffer.state = BufferState::RECEIVING;

            result = &buffer;

            break;
        }
    }

    xSemaphoreGive(m_bufferMutex);

    return result;
}

// publish completed frame
void Camera::publishFrame(
    FrameBuffer *buffer)
{
    int64_t t0 = esp_timer_get_time();
    xSemaphoreTake(
        m_bufferMutex,
        portMAX_DELAY);

    /*
     * If another READY frame exists, it is stale.
     *
     * Throw it away and keep the newest one.
     */
    for (auto &candidate : m_buffers)
    {
        if (
            &candidate != buffer &&
            candidate.state == BufferState::READY)
        {
            candidate.state = BufferState::FREE;
            candidate.size = 0;
        }
    }

    buffer->frame_number = ++m_frameCounter;

    buffer->state = BufferState::READY;

    xSemaphoreGive(m_bufferMutex);
    int64_t t1 = esp_timer_get_time();
    ESP_LOGI("PUBLISH_FRAME_TIME", "Publish time = %" PRId64 " us", t1 - t0);
}

const uint8_t *Camera::acquireLatestFrame(
    size_t &out_frame_size,
    uint32_t &out_frame_number)
{
    int64_t t0 = esp_timer_get_time();
    out_frame_size = 0;
    out_frame_number = 0;

    FrameBuffer *best = nullptr;

    xSemaphoreTake(
        m_bufferMutex,
        portMAX_DELAY);

    for (auto &buffer : m_buffers)
    {
        if (buffer.state != BufferState::READY)
        {
            continue;
        }

        if (
            best == nullptr ||
            buffer.frame_number > best->frame_number)
        {
            best = &buffer;
        }
    }

    if (best)
    {
        // Mark it READING so UART can never overwrite it.
         
        best->state = BufferState::READING;

        out_frame_size = best->size;
        out_frame_number = best->frame_number;
    }

    xSemaphoreGive(m_bufferMutex);

    if (!best)
    {
        return nullptr;
    }

    int64_t t1 = esp_timer_get_time();
    ESP_LOGI("ACQUIRE_LATEST_FRAME_TIME", "Acquire time = %" PRId64 " us", t1 - t0);
    return best->data;
}

void Camera::releaseFrame()
{
    int64_t t0 = esp_timer_get_time();
    xSemaphoreTake(
        m_bufferMutex,
        portMAX_DELAY);

    for (auto &buffer : m_buffers)
    {
        if (buffer.state == BufferState::READING)
        {
            buffer.state = BufferState::FREE;
            buffer.size = 0;

            break;
        }
    }

    xSemaphoreGive(m_bufferMutex);
    int64_t t1 = esp_timer_get_time();
    ESP_LOGI("RELEASE_FRAME_TIME", "Camera Release time = %" PRId64 " us", t1 - t0);
}

void Camera::executionLoop()
{

    /**
     * Search for the Header and keep searching till it sees it
     * Header seen: get the 'size' of incoming frame
     * Check if any buffer is FREE to store the incoming frame
     * If no buffer is FREE at that moment, discard the whole incoming frame and start again to header Search
     * If a buffer is FREE (f1), change it to RECEIVING, fetch the incoming frame.
     * If fetch incoming fails, force the same buffer to go back to FREE and jump back to header search
     * If the fetch incoming succeeds, update all READY buffer as FREE first, push the frame to the 'RECEIVING' buffer(f1) and update it to READY state using publishFrame().
     * 
     * FREE(idle) -> RECEIVING(preparing for new frame) -> READY(frame stored) -> READING(dispatch to screen) - FREE(completed, now free)
     * 
     * The READY buffer is the one to be sent for display. Call acquireLastFrame to retrieve the lastest READY and change the state to READING
     * Then trigger any 
     * After a frame is displayed, update its state from READING to FREE using releaseFrame()
     * */

    uint32_t incoming_len = 0; // 4bytes

    while (true)
    {
        const int64_t t0 = esp_timer_get_time();

        if (!findSync())
        {
            continue;
        }

        const int64_t t_sync = esp_timer_get_time();

        if (!readExact(
            // retrieve the 4byte 'size'
                reinterpret_cast<uint8_t *>(&incoming_len),
                sizeof(incoming_len)))
        {
            continue;
        }

        const int64_t t_size = esp_timer_get_time();

        if (incoming_len == 0 ||
            incoming_len > MAX_JPEG_SIZE)
        {
            ESP_LOGW(
                TAG,
                "Invalid JPEG size: %lu",
                static_cast<unsigned long>(incoming_len));

            continue;
        }

        FrameBuffer *buffer = getFreeBuffer();

        // if there is none of the 3 buffer allocations that is free(like replaceable), then discard the incoming frame
        if (!buffer)
        {
            uint8_t discard[512];

            size_t remaining = incoming_len;

            while (remaining > 0)
            {
                size_t chunk =
                    (remaining > sizeof(discard))
                        ? sizeof(discard)
                        : remaining;

                int received =
                    uart_read_bytes(
                        UART_PORT,
                        discard,
                        chunk,
                        portMAX_DELAY);

                if (received <= 0)
                {
                    break;
                }

                remaining -= received;
            }

            continue;
        }

        const int64_t t_buffer = esp_timer_get_time();

        if (!readExact(
                buffer->data,
                incoming_len))
        {
            xSemaphoreTake(
                m_bufferMutex,
                portMAX_DELAY);

            buffer->state = BufferState::FREE;

            xSemaphoreGive(m_bufferMutex);

            continue;
        }

        const int64_t t_payload = esp_timer_get_time();

        buffer->size = incoming_len;

        publishFrame(buffer);

        const int64_t t_publish = esp_timer_get_time();

        ESP_LOGI(
            "UART_TIMING",
            "sync=%" PRId64
            " us | size=%" PRId64
            " us | buffer=%" PRId64
            " us | payload=%" PRId64
            " us | publish=%" PRId64
            " us | TOTAL=%" PRId64
            " us | jpeg=%lu bytes",
            t_sync - t0,
            t_size - t_sync,
            t_buffer - t_size,
            t_payload - t_buffer,
            t_publish - t_payload,
            t_publish - t0,
            static_cast<unsigned long>(incoming_len));
    }
}

bool Camera::isValidJpeg(
    const uint8_t *data,
    size_t size)
{
    int64_t t0 = esp_timer_get_time();
    if (data == nullptr || size < 4)
    {
        return false;
    }

    if (
        data[0] != 0xFF ||
        data[1] != 0xD8)
    {
        return false;
    }

    if (
        data[size - 2] != 0xFF ||
        data[size - 1] != 0xD9)
    {
        return false;
    }
    int64_t t1 = esp_timer_get_time();
    ESP_LOGI("IS_VALID_JPEG_TIME", "jpeg validation = %" PRId64 " us", t1 - t0);

    return true;
}

// I (34230) RELEASE_FRAME_TIME: Camera Release time = 11 us
// I (34230) RELEASE_FRAME_TIME: Camera Release time = 7 us
// I (34230) PUBLISH_FRAME_TIME: Publish time = 13 us
// I (34240) ACQUIRE_LATEST_FRAME_TIME: Acquire time = 9 us
// I (34240) UART_TIMING: sync=13053 us | size=15 us | buffer=16 us | payload=34507 us | publish=7592 us | TOTAL=55183 us | jpeg=6823 bytes
// I (34250) IS_VALID_JPEG_TIME: jpeg validation = 5 us
// I (34290) DRAW_REALTIME_CAMERA_TIME: header=292 us | decode=18994 us | lcd=13469 us | decode+lcd=32463 us | TOTAL=32770 us | frame_size=6823 | frame_number=394





// I (471810) RELEASE_FRAME_TIME: Camera Release time = 11 us
// I (471810) ACQUIRE_LATEST_FRAME_TIME: Acquire time = 15 us
// I (471810) READ EXACT: Seen? 1007493564 len =2920
// I (471820) IS_VALID_JPEG_TIME: jpeg validation = 5 us
// I (471820) PUBLISH_FRAME_TIME: Publish time = 45 us
// I (471830) UART_TIMING: sync=12994 us | size=214 us | buffer=18 us | payload=21962 us | publish=9435 us | TOTAL=44623 us | jpeg=2920 bytes
// I (471850) READ EXACT: Seen? 1070513584 len =4
// I (471860) DRAW_REALTIME_CAMERA_TIME: header=312 us | decode=17128 us | lcd=17906 us | decode+lcd=35034 us | TOTAL=35361 us | frame_size=2928 | frame_number=9296
// I (471860) READ EXACT: Seen? 1007362484 len =2934
// I (471860) RELEASE_FRAME_TIME: Camera Release time = 13 us
// I (471870) PUBLISH_FRAME_TIME: Publish time = 12 us
// I (471870) ACQUIRE_LATEST_FRAME_TIME: Acquire time = 17 us
// I (471880) UART_TIMING: sync=6288 us | size=261 us | buffer=28 us | payload=19645 us | publish=9809 us | TOTAL=36031 us | jpeg=2934 bytes
// I (471880) IS_VALID_JPEG_TIME: jpeg validation = 5 us
// I (471900) READ EXACT: Seen? 1070513584 len =4
// I (471910) READ EXACT: Seen? 1007428024 len =2918
// I (471910) PUBLISH_FRAME_TIME: Publish time = 9 us
// I (471910) UART_TIMING: sync=2886 us | size=6126 us | buffer=26 us | payload=8569 us | publish=270 us | TOTAL=17877 us | jpeg=2918 bytes
// I (471930) DRAW_REALTIME_CAMERA_TIME: header=338 us | decode=16923 us | lcd=13407 us | decode+lcd=30330 us | TOTAL=30682 us | frame_size=2934 | frame_number=9298
// I (471940) RELEASE_FRAME_TIME: Camera Release time = 11 us
// I (471940) ACQUIRE_LATEST_FRAME_TIME: Acquire time = 17 us
