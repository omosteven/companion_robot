#pragma once

#include <cstdint>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_jpeg_dec.h"

class Display
{
public:
    Display();
    ~Display();

    // Hardware resources should not be copied.
    Display(const Display &) = delete;
    Display &operator=(const Display &) = delete;

    // Initialize the SPI bus and ILI9488 panel.
    esp_err_t init();
    //  the entire display with a color.
    esp_err_t clear(uint16_t color);
    // Draw a single pixel.
    esp_err_t draw_pixel(
        int x,
        int y,
        uint16_t color);
    // Draw an RGB565 bitmap.
    esp_err_t draw_bitmap(
        int x,
        int y,
        int width,
        int height,
        const uint16_t *bitmap);

    esp_err_t fill_rect(int x, int y, int width, int height, uint16_t color);
    esp_err_t draw_circle(int xc, int yc, int r, uint16_t color);
    esp_err_t fill_circle(int xc, int yc, int r, uint16_t color);
    esp_err_t render();

    void draw_default_eyes(int look_offset_x = 0, int look_offset_y = 0, bool use_clear = true);
    void animate_blink(int hold_delay_ms = 120, bool use_clear = true);
    void animate_look_around();
    void animate_look_up_down();
    void animate_pupil_move(int target_x, int target_y, int duration_ms);

    void draw_grid_layout(int mode, uint16_t line_color);
    void fill_grid_cell(int mode, int index, uint16_t color);

    void draw_string(const char *text, int start_x, int start_y, int font_size, uint16_t text_color);

    esp_err_t draw_realtime_camera(const uint8_t *jpeg_data, size_t jpeg_size, uint32_t frame_number);

    bool is_initialized() const;

    int width() const;

    int height() const;

    jpeg_dec_handle_t jpeg_decoder_ = nullptr;

private:
    esp_lcd_panel_io_handle_t panel_io_;

    esp_lcd_panel_handle_t panel_;

    bool initialized_;

    bool eyes_drawn_ = false;

    int prev_look_x_ = 0; // Stores last known X offset
    int prev_look_y_ = 0;

    uint16_t *canvas_buffer_ = nullptr;
    size_t canvas_size_pixels_ = 0;

    uint8_t *camera_rgb565_buffer_ = nullptr;

    size_t camera_rgb565_buffer_size_ = 0;
};