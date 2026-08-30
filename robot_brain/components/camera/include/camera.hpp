#pragma once

#include <cstdint>
#include <cstddef>

#include "driver/uart.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

class Camera
{
public:
    static constexpr uart_port_t UART_PORT = UART_NUM_1;

    static constexpr int BAUD_RATE = 2000000;
    // static constexpr int BAUD_RATE = 115200;

    static constexpr int RX_PIN = 18;
    static constexpr int TX_PIN = 17;


    static constexpr size_t RX_BUF_SIZE = 32768;

 
    static constexpr size_t MAX_JPEG_SIZE = 128 * 1024; //128kb

    static constexpr int BUFFER_COUNT = 3;

    Camera();
    ~Camera();

    esp_err_t init();

    esp_err_t startTask();

    const uint8_t *acquireLatestFrame(
        size_t &out_frame_size,
        uint32_t &out_frame_number);

    void releaseFrame();

    bool isValidJpeg(const uint8_t *data,
                     size_t size);

private:
    enum class BufferState : uint8_t
    {
        FREE,
        RECEIVING,
        READY,
        READING
    };

    struct FrameBuffer
    {
        uint8_t *data;
        size_t size;
        uint32_t frame_number;
        BufferState state;
    };

    static void rxTaskTrampoline(void *pvParameters);

    void executionLoop();

    bool readExact(
        uint8_t *buffer,
        size_t length);

    bool findSync();

    FrameBuffer *getFreeBuffer();

    void publishFrame(FrameBuffer *buffer);

    FrameBuffer m_buffers[BUFFER_COUNT]; // [ { *data, size, frame_number, state}, { *data, size, frame_number, state}, { *data, size, frame_number, state} ]

    SemaphoreHandle_t m_bufferMutex;

    TaskHandle_t m_taskHandle;

    bool m_isInitialized;

    uint32_t m_frameCounter;
};