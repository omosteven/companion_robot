#include "display.hpp"
#include "display_conf.hpp"

#include <cstdint>

#include "esp_check.h"
#include "esp_log.h"

#include "driver/spi_master.h"

#include "esp_heap_caps.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#include "esp_lcd_ili9488.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "colors.hpp"
#include "font8x8.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <inttypes.h>
#include "esp_jpeg_dec.h"

static const char *TAG = "Display";

Display::Display()
    : panel_io_(nullptr),
      panel_(nullptr),
      initialized_(false)
{

    jpeg_dec_config_t config =
        DEFAULT_JPEG_DEC_CONFIG();

    config.output_type =
        JPEG_PIXEL_FORMAT_RGB565_LE;

    jpeg_error_t ret =
        jpeg_dec_open(
            &config,
            &jpeg_decoder_);

    ESP_LOGI(
        "JPEG",
        "decoder=%p",
        jpeg_decoder_);

    if (ret != JPEG_ERR_OK)
    {
        jpeg_decoder_ = nullptr;

        ESP_LOGE(
            "Display",
            "Failed to initialize JPEG decoder: %d",
            ret);
    }
    else
    {
        ESP_LOGI(
            "Display",
            "JPEG decoder initialized");
    }
}

Display::~Display()
{
    if (panel_ != nullptr)
    {
        esp_lcd_panel_del(panel_);
        panel_ = nullptr;
    }

    if (panel_io_ != nullptr)
    {
        esp_lcd_panel_io_del(panel_io_);
        panel_io_ = nullptr;
    }

    if (canvas_buffer_ != nullptr)
    {
        free(canvas_buffer_);
        canvas_buffer_ = nullptr;
    }

    if (camera_rgb565_buffer_ != nullptr)
    {
        // free(camera_rgb565_buffer_);
        jpeg_free_align(camera_rgb565_buffer_);

        camera_rgb565_buffer_ = nullptr;
    }

    if (jpeg_decoder_ != nullptr)
    {
        jpeg_dec_close(jpeg_decoder_);
        jpeg_decoder_ = nullptr;
    }
}

esp_err_t Display::init()
{
    if (initialized_)
    {
        return ESP_OK;
    }

    ESP_LOGI(
        TAG,
        "Initializing ILI9488 display...");

    spi_bus_config_t bus_config{};
    bus_config.sclk_io_num =
        DisplayConfig::PIN_SCLK;
    bus_config.mosi_io_num =
        DisplayConfig::PIN_MOSI;
    bus_config.miso_io_num =
        DisplayConfig::PIN_MISO;
    bus_config.quadwp_io_num = GPIO_NUM_NC;
    bus_config.quadhd_io_num = GPIO_NUM_NC;
    bus_config.max_transfer_sz =
        DisplayConfig::WIDTH *
        DisplayConfig::HEIGHT *
        3;

    ESP_RETURN_ON_ERROR(
        spi_bus_initialize(
            DisplayConfig::SPI_HOST,
            &bus_config,
            SPI_DMA_CH_AUTO),
        TAG,
        "Failed to initialize SPI bus");

    ESP_LOGI(
        TAG,
        "SPI bus initialized");

    // Configure SPI LCD panel IO

    esp_lcd_panel_io_spi_config_t io_config{};
    io_config.cs_gpio_num =
        DisplayConfig::PIN_CS;
    io_config.dc_gpio_num =
        DisplayConfig::PIN_DC;
    io_config.spi_mode = 0;
    io_config.pclk_hz =
        DisplayConfig::SPI_FREQUENCY_HZ;
    io_config.trans_queue_depth =
        DisplayConfig::TRANS_QUEUE_DEPTH;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi(
            DisplayConfig::SPI_HOST,
            &io_config,
            &panel_io_),
        TAG,
        "Failed to create LCD SPI IO");

    ESP_LOGI(
        TAG,
        "LCD SPI IO initialized");

    // Configure ILI9488

    esp_lcd_panel_dev_config_t panel_config = {};

    panel_config.reset_gpio_num =
        DisplayConfig::PIN_RST;

    // the ILI9488 SPI uses 18-bit color mode.
    panel_config.bits_per_pixel = 18;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel_config.data_endian = LCD_RGB_DATA_ENDIAN_BIG;
    panel_config.flags.reset_active_high = 0;

    // Create ILI9488 panel
    constexpr size_t COLOR_BUFFER_SIZE = DisplayConfig::WIDTH * 40 * 3;

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_ili9488(
            panel_io_,
            &panel_config,
            COLOR_BUFFER_SIZE,
            &panel_),
        TAG,
        "Failed to create ILI9488 panel");

    ESP_LOGI(
        TAG,
        "ILI9488 panel created");

    // Hardware reset
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_reset(panel_),
        TAG,
        "Failed to reset LCD");

    // Initialize ILI9488
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_init(panel_),
        TAG,
        "Failed to initialize ILI9488");

    // Set Orientation
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_swap_xy(
            panel_,
            true),
        TAG,
        "Failed to configure XY swap");

    // onfigure display mirror
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_mirror(
            panel_,
            true,
            false),
        TAG,
        "Failed to configure display mirror");

    // Turn display ON
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_disp_on_off(
            panel_,
            true),
        TAG,
        "Failed to turn display on");

    ESP_LOGI(TAG, "Forcing physical ILI9488 MADCTL Landscape register command...");

    // Command 0x36 is MADCTL (Memory Access Control register)
    // 0x28 or 0x48 sets the chip explicitly into Landscape (BGR mode)
    uint8_t madctl_landscape_param = 0x28;
    // Force hardware register for landscape
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_tx_param(panel_io_, 0x36, &madctl_landscape_param, 1),
        TAG,
        "Failed to force hardware landscape register");

    initialized_ = true;

    ESP_LOGI(
        TAG,
        "ILI9488 initialized successfully: %dx%d",
        DisplayConfig::WIDTH,
        DisplayConfig::HEIGHT);

    // set the canvas pixel dimension
    canvas_size_pixels_ = DisplayConfig::WIDTH * DisplayConfig::HEIGHT;

    ESP_LOGI(TAG, "Allocating 153.6KB Canvas buffer in external PSRAM...");

    // allocate memory onto the PSRAM for the canvas buffer. This will be a backup so the DMA controller can read it directly.
    canvas_buffer_ = (uint16_t *)heap_caps_malloc(
        canvas_size_pixels_ * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (canvas_buffer_ == nullptr)
    {
        ESP_LOGE(TAG, "CRITICAL: PSRAM allocation failed! Trying fallback allocation...");

        // Emergency fallback if the specific board variant requires generic allocation
        canvas_buffer_ = (uint16_t *)malloc(canvas_size_pixels_ * sizeof(uint16_t));
    }

    if (canvas_buffer_ == nullptr)
    {
        ESP_LOGE(TAG, "FATAL: Complete out of memory state for Canvas buffer!");
        return ESP_ERR_NO_MEM;
    }

    // Safely clear the freshly mapped memory canvas array to clean black
    for (size_t i = 0; i < canvas_size_pixels_; ++i)
    {
        canvas_buffer_[i] = Color::BLACK;
    }

    ESP_LOGI(TAG, "PSRAM Canvas successfully allocated at memory address: %p", canvas_buffer_);

    // camera buffer
    camera_rgb565_buffer_size_ =
        320 * 240 * 2;

    // camera_rgb565_buffer_ =
    //     static_cast<uint8_t *>(
    //         heap_caps_malloc(
    //             camera_rgb565_buffer_size_,
    //             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    camera_rgb565_buffer_ =
        static_cast<uint8_t *>(
            jpeg_calloc_align(
                camera_rgb565_buffer_size_,
                16));

    if (!camera_rgb565_buffer_)
    {
        ESP_LOGE(
            TAG,
            "Failed to allocate persistent camera RGB565 buffer");

        return ESP_ERR_NO_MEM;
    }
    initialized_ = true;

    return ESP_OK;
}

// Draw pixel
esp_err_t Display::draw_pixel(
    int x,
    int y,
    uint16_t color)
{
    if (!initialized_)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (x < 0 ||
        x >= DisplayConfig::WIDTH ||
        y < 0 ||
        y >= DisplayConfig::HEIGHT)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t pixel = color;

    return esp_lcd_panel_draw_bitmap(
        panel_,
        x,
        y,
        x + 1,
        y + 1,
        &pixel);
}

// Draw bitmap
esp_err_t Display::draw_bitmap(
    int x,
    int y,
    int width,
    int height,
    const uint16_t *bitmap)
{
    if (!initialized_)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (bitmap == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (width <= 0 || height <= 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (x < 0 ||
        y < 0 ||
        x + width > DisplayConfig::WIDTH ||
        y + height > DisplayConfig::HEIGHT)
    {
        return ESP_ERR_INVALID_ARG;
    }

    return esp_lcd_panel_draw_bitmap(
        panel_,
        x,
        y,
        x + width,
        y + height,
        bitmap);
}

esp_err_t Display::render()
{
    // retrieves the data  from PSRAM, load chunks(slice) to the SRAM so the DMA can send to the display
    if (!initialized_ || !canvas_buffer_)
        return ESP_ERR_INVALID_STATE;

    // Dimensions of our transmission window slices
    constexpr int WIDTH = DisplayConfig::WIDTH;
    constexpr int HEIGHT = DisplayConfig::HEIGHT;
    constexpr int SLICE_ROWS = 10; // Process 10 rows at a time to fasten display instead of processing all pixels at once

    // inside the internal un-crashable SRAM cache pool.
    static uint16_t internal_sram_buffer[WIDTH * SLICE_ROWS];

    // Transmit the canvas in sequential slices to protect the SPI bus
    for (int y = 0; y < HEIGHT; y += SLICE_ROWS)
    {
        int current_rows = (y + SLICE_ROWS <= HEIGHT) ? SLICE_ROWS : (HEIGHT - y);
        int total_pixels_to_copy = WIDTH * current_rows;

        // Calculate the starting pointer position out in your PSRAM warehouse
        uint16_t *psram_source_ptr = canvas_buffer_ + (y * WIDTH);

        // Safe internal copy loop to bridge memory blocks
        for (int i = 0; i < total_pixels_to_copy; ++i)
        {
            internal_sram_buffer[i] = psram_source_ptr[i];
        }

        // Send the clean, internal buffer segment down the SPI channel
        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_draw_bitmap(
                panel_,
                0, y,
                WIDTH, y + current_rows,
                internal_sram_buffer),
            TAG, "Failed to flush hardware DMA slice");

        // Feed the watchdog to prevent system lockups
        // vTaskDelay(pdMS_TO_TICKS(1));
        taskYIELD();
    }

    return ESP_OK;
}

// Clear whatever on the display
esp_err_t Display::clear(uint16_t color)
{
    if (!initialized_ || !canvas_buffer_)
        return ESP_ERR_INVALID_STATE;

    for (size_t i = 0; i < canvas_size_pixels_; ++i)
    {
        canvas_buffer_[i] = color;
    }
    return ESP_OK;
}

esp_err_t Display::fill_rect(int x, int y, int width, int height, uint16_t color)
{
    if (!initialized_ || !canvas_buffer_)
        return ESP_ERR_INVALID_STATE;
    if (width <= 0 || height <= 0)
        return ESP_ERR_INVALID_ARG;

    int x1 = x;
    int y1 = y;
    int x2 = x + width;
    int y2 = y + height;
    if (x1 < 0)
        x1 = 0;
    if (y1 < 0)
        y1 = 0;
    if (x2 > DisplayConfig::WIDTH)
        x2 = DisplayConfig::WIDTH;
    if (y2 > DisplayConfig::HEIGHT)
        y2 = DisplayConfig::HEIGHT;

    for (int row = y1; row < y2; ++row)
    {
        int row_offset = row * DisplayConfig::WIDTH;
        for (int col = x1; col < x2; ++col)
        {
            canvas_buffer_[row_offset + col] = color;
        }
    }
    return ESP_OK;
}

esp_err_t Display::fill_circle(int xc, int yc, int r, uint16_t color)
{
    if (!initialized_ || !canvas_buffer_)
        return ESP_ERR_INVALID_STATE;

    int x = 0;
    int y = r;
    int d = 3 - 2 * r;

    auto draw_canvas_line = [this](int x1, int x2, int y_row, uint16_t col)
    {
        if (y_row >= 0 && y_row < DisplayConfig::HEIGHT)
        {
            if (x1 < 0)
                x1 = 0;
            if (x2 >= DisplayConfig::WIDTH)
                x2 = DisplayConfig::WIDTH - 1;
            if (x1 <= x2)
            {
                int row_offset = y_row * DisplayConfig::WIDTH;
                for (int i = x1; i <= x2; ++i)
                {
                    this->canvas_buffer_[row_offset + i] = col;
                }
            }
        }
    };

    while (y >= x)
    {
        draw_canvas_line(xc - x, xc + x, yc + y, color);
        draw_canvas_line(xc - x, xc + x, yc - y, color);
        draw_canvas_line(xc - y, xc + y, yc + x, color);
        draw_canvas_line(xc - y, xc + y, yc - x, color);

        x++;
        if (d > 0)
        {
            y--;
            d = d + 4 * (x - y) + 10;
        }
        else
        {
            d = d + 4 * x + 6;
        }
    }
    return ESP_OK;
}

void Display::draw_default_eyes(int look_offset_x, int look_offset_y, bool use_clear)
{
    if (!initialized_)
        return;

    const int LEFT_EYE_CENTER_X = 140;
    const int RIGHT_EYE_CENTER_X = 340;
    const int EYE_CENTER_Y = 160;
    const int EYE_RADIUS = 55;
    const int PUPIL_RADIUS = 30;

    // Silent, flawless memory canvas render updates
    if (use_clear)
        this->clear(Color::BLACK);
    this->fill_circle(LEFT_EYE_CENTER_X, EYE_CENTER_Y, EYE_RADIUS, Color::WHITE);
    this->fill_circle(RIGHT_EYE_CENTER_X, EYE_CENTER_Y, EYE_RADIUS, Color::WHITE);
    this->fill_circle(LEFT_EYE_CENTER_X + look_offset_x, EYE_CENTER_Y + look_offset_y, PUPIL_RADIUS, Color::BLUE);
    this->fill_circle(RIGHT_EYE_CENTER_X + look_offset_x, EYE_CENTER_Y + look_offset_y, PUPIL_RADIUS, Color::BLUE);

    this->render();
}

void Display::animate_pupil_move(int target_x, int target_y, int duration_ms)
{
    if (!initialized_ || !canvas_buffer_)
        return;

    int start_x = prev_look_x_;
    int start_y = prev_look_y_;

    // Target 30 frames per second (~33ms per frame step)
    int steps = duration_ms / 33;
    if (steps < 1)
        steps = 1;

    for (int i = 1; i <= steps; ++i)
    {
        float t = (float)i / steps;
        int current_x = start_x + (int)((target_x - start_x) * t);
        int current_y = start_y + (int)((target_y - start_y) * t);

        this->draw_default_eyes(current_x, current_y, false);

        // gives FreeRTOS 33ms to handle the Watchdog and stream your data out over SPI.
        vTaskDelay(pdMS_TO_TICKS(33));
    }

    this->draw_default_eyes(target_x, target_y, false);
}

void Display::animate_blink(int hold_delay_ms, bool use_clear)
{
    // This builds up the opening and closing onto the PSRAM memory amd memset clears bytes block-by-block instantly in RAM
    if (!initialized_ || !canvas_buffer_)
        return;

    const int LEFT_EYE_CENTER_X = 140;
    const int RIGHT_EYE_CENTER_X = 340;
    const int EYE_CENTER_Y = 160;
    const int EYE_RADIUS = 55;
    const int PUPIL_RADIUS = 30;

    // Boundary constraints of the eye box footprint
    const int BOX_W = EYE_RADIUS * 2; // 110 pixels wide

    // Double the stepping speed (+=10) to make the closing look snappy
    for (int slice_amount = 0; slice_amount <= EYE_RADIUS; slice_amount += 10)
    {

        // Render the base eyes onto the background memory cache
        if (use_clear)
            this->clear(Color::BLACK);
        this->fill_circle(LEFT_EYE_CENTER_X, EYE_CENTER_Y, EYE_RADIUS, Color::WHITE);
        this->fill_circle(RIGHT_EYE_CENTER_X, EYE_CENTER_Y, EYE_RADIUS, Color::WHITE);
        this->fill_circle(LEFT_EYE_CENTER_X + prev_look_x_, EYE_CENTER_Y + prev_look_y_, PUPIL_RADIUS, Color::BLUE);
        this->fill_circle(RIGHT_EYE_CENTER_X + prev_look_x_, EYE_CENTER_Y + prev_look_y_, PUPIL_RADIUS, Color::BLUE);

        // Wipe local top and bottom sections directly inside the RAM grid arrays
        for (int h = 0; h < slice_amount; ++h)
        {
            // Squeeze top down
            int top_y = (EYE_CENTER_Y - EYE_RADIUS) + h;
            memset(canvas_buffer_ + (top_y * DisplayConfig::WIDTH) + (LEFT_EYE_CENTER_X - EYE_RADIUS), 0, BOX_W * sizeof(uint16_t));
            memset(canvas_buffer_ + (top_y * DisplayConfig::WIDTH) + (RIGHT_EYE_CENTER_X - EYE_RADIUS), 0, BOX_W * sizeof(uint16_t));

            // Squeeze bottom up
            int bot_y = (EYE_CENTER_Y + EYE_RADIUS) - h;
            memset(canvas_buffer_ + (bot_y * DisplayConfig::WIDTH) + (LEFT_EYE_CENTER_X - EYE_RADIUS), 0, BOX_W * sizeof(uint16_t));
            memset(canvas_buffer_ + (bot_y * DisplayConfig::WIDTH) + (RIGHT_EYE_CENTER_X - EYE_RADIUS), 0, BOX_W * sizeof(uint16_t));
        }

        // Flush layout to display panel
        this->render();
        // vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (use_clear)
        this->clear(Color::BLACK);
    this->fill_rect(LEFT_EYE_CENTER_X - EYE_RADIUS, EYE_CENTER_Y - 1, EYE_RADIUS * 2, 2, Color::BLUE);
    this->fill_rect(RIGHT_EYE_CENTER_X - EYE_RADIUS, EYE_CENTER_Y - 1, EYE_RADIUS * 2, 2, Color::BLUE);
    this->render();

    vTaskDelay(pdMS_TO_TICKS(hold_delay_ms));

    // REOPENING PHASE (Expanding outward)
    for (int slice_amount = EYE_RADIUS; slice_amount >= 0; slice_amount -= 10)
    {
        // this->clear(Color::BLACK);
        this->fill_circle(LEFT_EYE_CENTER_X, EYE_CENTER_Y, EYE_RADIUS, Color::WHITE);
        this->fill_circle(RIGHT_EYE_CENTER_X, EYE_CENTER_Y, EYE_RADIUS, Color::WHITE);
        this->fill_circle(LEFT_EYE_CENTER_X + prev_look_x_, EYE_CENTER_Y + prev_look_y_, PUPIL_RADIUS, Color::BLUE);
        this->fill_circle(RIGHT_EYE_CENTER_X + prev_look_x_, EYE_CENTER_Y + prev_look_y_, PUPIL_RADIUS, Color::BLUE);

        for (int h = 0; h < slice_amount; ++h)
        {
            int top_y = (EYE_CENTER_Y - EYE_RADIUS) + h;
            memset(canvas_buffer_ + (top_y * DisplayConfig::WIDTH) + (LEFT_EYE_CENTER_X - EYE_RADIUS), 0, BOX_W * sizeof(uint16_t));
            memset(canvas_buffer_ + (top_y * DisplayConfig::WIDTH) + (RIGHT_EYE_CENTER_X - EYE_RADIUS), 0, BOX_W * sizeof(uint16_t));

            int bot_y = (EYE_CENTER_Y + EYE_RADIUS) - h;
            memset(canvas_buffer_ + (bot_y * DisplayConfig::WIDTH) + (LEFT_EYE_CENTER_X - EYE_RADIUS), 0, BOX_W * sizeof(uint16_t));
            memset(canvas_buffer_ + (bot_y * DisplayConfig::WIDTH) + (RIGHT_EYE_CENTER_X - EYE_RADIUS), 0, BOX_W * sizeof(uint16_t));
        }

        this->render();
        // vTaskDelay(pdMS_TO_TICKS(5));
    }

    // Force snap lock back to absolute target view layout coordinates
    this->draw_default_eyes(prev_look_x_, prev_look_y_, use_clear);
}

void Display::animate_look_around()
{

    this->animate_pupil_move(20, 0, 80);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 2. Smoothly slide eyes all the way to the Left over 800 milliseconds
    this->animate_pupil_move(-20, 0, 80);
    vTaskDelay(pdMS_TO_TICKS(200));

    // 3. Smoothly slide back to the Center over 400 milliseconds
    this->animate_pupil_move(0, 0, 80);
    vTaskDelay(pdMS_TO_TICKS(100));
}

void Display::animate_look_up_down()
{

    this->animate_pupil_move(0, 20, 80);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 2. Smoothly slide eyes all the way to the Left over 800 milliseconds
    this->animate_pupil_move(0, -20, 80);
    vTaskDelay(pdMS_TO_TICKS(200));

    // 3. Smoothly slide back to the Center over 400 milliseconds
    this->animate_pupil_move(0, 0, 80);
    vTaskDelay(pdMS_TO_TICKS(100));
}

void Display::draw_grid_layout(int mode, uint16_t line_color)
{
    if (!initialized_ || canvas_buffer_)
        return;

    this->clear(Color::BLACK);

    constexpr int THICKNESS = 2;

    if (mode == 4)
    {
        // each cell: 240 x160
        // vertical center line
        this->fill_rect(240 - (THICKNESS / 2), 0, THICKNESS, 320, line_color);
        // horizontal center line
        this->fill_rect(0, 160 - (THICKNESS / 2), 480, THICKNESS, line_color);
    }
    else if (mode == 6)
    {
        //  3columns, 2 rows. Each cell: 160x160
        this->fill_rect(160 - (THICKNESS / 2), 0, THICKNESS, 320, line_color);
        this->fill_rect(320 - (THICKNESS / 2), 0, THICKNESS, 320, line_color);
        this->fill_rect(0, 160 - (THICKNESS / 2), 480, THICKNESS, line_color);
    }
    else if (mode == 8)
    {
        // each cehll: 120x160
        // vertical lines
        this->fill_rect(120 - (THICKNESS / 2), 0, THICKNESS, 320, line_color);
        this->fill_rect(240 - (THICKNESS / 2), 0, THICKNESS, 320, line_color);
        this->fill_rect(360 - (THICKNESS / 2), 0, THICKNESS, 320, line_color);
        // Horizontal center line
        this->fill_rect(0, 160 - (THICKNESS / 2), 480, THICKNESS, line_color);
    }
}

void Display::fill_grid_cell(int mode, int index, uint16_t color)
{
    if (!initialized_ || !canvas_buffer_)
        return;

    int cell_x = 0;
    int cell_y = 0;
    int cell_w = 0;
    int cell_h = 0;

    // Pad inner cells slightly by 2 pixels so we don't overwrite our grid borders
    constexpr int PAD = 2;

    if (mode == 4)
    {
        if (index < 0 || index >= 4)
            return;
        cell_w = 240;
        cell_h = 160;
        cell_x = (index % 2) * cell_w;
        cell_y = (index / 2) * cell_h;
    }
    else if (mode == 6)
    {
        if (index < 0 || index >= 6)
            return;
        cell_w = 160;
        cell_h = 160;
        cell_x = (index % 3) * cell_w;
        cell_y = (index / 3) * cell_h;
    }
    else if (mode == 8)
    {
        if (index < 0 || index >= 8)
            return;
        cell_w = 120;
        cell_h = 160;
        cell_x = (index % 4) * cell_w;
        cell_y = (index / 4) * cell_h;
    }

    // Apply the inner cell padding fill
    this->fill_rect(cell_x + PAD, cell_y + PAD, cell_w - (PAD * 2), cell_h - (PAD * 2), color);
}

void Display::draw_string(const char *text, int start_x, int start_y, int font_size, uint16_t text_color)
{
    if (!initialized_ || !canvas_buffer_ || text == nullptr)
        return;
    if (font_size <= 0)
        font_size = 1;

    int cursor_x = start_x;
    int cursor_y = start_y;

    // Loop through each individual letter character inside the word string text
    while (*text != '\0')
    {
        char character = *text;

        // Ensure character falls inside printable ASCII bounds (Space to z)
        if (character >= 32 && character <= 125)
        {
            int ascii_index = character - 32; // Offset tracking index map match

            // Scan through the 8 vertical matrix byte rows of the font character map
            for (int row = 0; row < 8; ++row)
            {
                uint8_t byte_line = Font8x8::ASCII[ascii_index][row];

                // Scan through each of the 8 bits inside the byte row (Left to Right)
                for (int bit = 0; bit < 8; ++bit)
                {
                    // Check if bit status is high (1 = Text pixel, 0 = Background)
                    if (byte_line & (0x80 >> bit))
                    {

                        // Calculate scaled position coordinates matching your font size inputs
                        int target_x = cursor_x + (bit * font_size);
                        int target_y = cursor_y + (row * font_size);

                        // Draw the scaled font pixel directly onto your background canvas buffer
                        // uses a small nested block write to achieve variable sizing adjustments
                        for (int sy = 0; sy < font_size; ++sy)
                        {
                            int final_y = target_y + sy;
                            if (final_y < 0 || final_y >= DisplayConfig::HEIGHT)
                                continue;

                            int row_offset = final_y * DisplayConfig::WIDTH;

                            for (int sx = 0; sx < font_size; ++sx)
                            {
                                int final_x = target_x + sx;
                                if (final_x < 0 || final_x >= DisplayConfig::WIDTH)
                                    continue;

                                canvas_buffer_[row_offset + final_x] = text_color;
                            }
                        }
                    }
                }
            }
        }

        //  Shift cursor position right to space out the next letter character
        // Standard width is 8 pixels * font size multiplier + 2 padding tracking spacer pixels
        cursor_x += (8 * font_size) + (2 * font_size);

        // Auto wrap sentence tracking line if the text passes your 480 boundary length width
        if (cursor_x >= DisplayConfig::WIDTH - (8 * font_size))
        {
            cursor_x = start_x;                            // Reset to margin line boundary
            cursor_y += (8 * font_size) + (4 * font_size); // Shift down one row line length
        }

        text++; // Process the next letter char
    }
}

esp_err_t Display::draw_realtime_camera(
    const uint8_t *jpeg_data,
    size_t jpeg_size,
    uint32_t frame_number)
{
    int64_t t_start = esp_timer_get_time();

    jpeg_dec_io_t io = {};
    jpeg_dec_header_info_t header = {};

    io.inbuf = const_cast<uint8_t *>(jpeg_data);
    io.inbuf_len = jpeg_size;

    int64_t t_header_start = esp_timer_get_time();

    jpeg_error_t ret =
        jpeg_dec_parse_header(
            jpeg_decoder_,
            &io,
            &header);

    int64_t t_header_end = esp_timer_get_time();

    if (ret != JPEG_ERR_OK)
    {
        ESP_LOGE("JPEG", "parse_header failed: %d", ret);
        return ESP_FAIL;
    }

    if (header.width > DisplayConfig::WIDTH ||
        header.height > DisplayConfig::HEIGHT)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    io.outbuf = camera_rgb565_buffer_;
    io.out_size = 320 * 240 * 2;

    int64_t t_decode_start = esp_timer_get_time();

    ret =
        jpeg_dec_process(
            jpeg_decoder_,
            &io);

    int64_t t_decode_end = esp_timer_get_time();

    if (ret != JPEG_ERR_OK)
    {
        ESP_LOGE("JPEG", "decode failed: %d", ret);
        return ESP_FAIL;
    }

    const int start_x =
        (DisplayConfig::WIDTH - header.width) / 2;

    const int start_y =
        (DisplayConfig::HEIGHT - header.height) / 2;

    int64_t t_lcd_start = esp_timer_get_time();

    esp_err_t err =
        esp_lcd_panel_draw_bitmap(
            panel_,
            start_x,
            start_y,
            start_x + header.width,
            start_y + header.height,
            camera_rgb565_buffer_);

    int64_t t_lcd_end = esp_timer_get_time();

    int64_t t_end = esp_timer_get_time();

    ESP_LOGI(
        "DRAW_REALTIME_CAMERA_TIME",
        "header=%" PRId64
        " us | decode=%" PRId64
        " us | lcd=%" PRId64
        " us | decode+lcd=%" PRId64
        " us | TOTAL=%" PRId64
        " us | frame_size=%zu | frame_number=%u",

        t_header_end - t_header_start,
        t_decode_end - t_decode_start,
        t_lcd_end - t_lcd_start,
        (t_decode_end - t_decode_start) +
            (t_lcd_end - t_lcd_start),
        t_end - t_start,
        jpeg_size,
        frame_number);

    return err;
}

bool Display::is_initialized() const
{
    return initialized_;
}

// Dimensions
int Display::width() const
{
    return DisplayConfig::WIDTH;
}

int Display::height() const
{
    return DisplayConfig::HEIGHT;
}