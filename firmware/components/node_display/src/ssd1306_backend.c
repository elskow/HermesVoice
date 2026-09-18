#include "ssd1306_compat.h"

#ifndef NODE_HOST_TEST
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lcd_panel_vendor.h"

#define OLED_ADDR 0x3C

static esp_lcd_panel_handle_t s_panel;

int ssd1306_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_NODE_OLED_SDA_GPIO,
        .scl_io_num = CONFIG_NODE_OLED_SCL_GPIO,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        return -1;
    }
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = OLED_ADDR,
        .scl_speed_hz = 400000,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    if (esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io) != ESP_OK) {
        return -1;
    }
    esp_lcd_panel_dev_config_t panel_cfg = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
    };
    if (esp_lcd_new_panel_ssd1306(io, &panel_cfg, &s_panel) != ESP_OK) {
        return -1;
    }
    if (esp_lcd_panel_reset(s_panel) != ESP_OK
        || esp_lcd_panel_init(s_panel) != ESP_OK
        || esp_lcd_panel_disp_on_off(s_panel, true) != ESP_OK) {
        return -1;
    }
    return 0;
}

static uint8_t page_buf[8][128];

void ssd1306_flush(char lines[NODE_DISPLAY_ROWS][NODE_DISPLAY_COLS + 1])
{
    extern void font_render_page(const char *text, uint8_t *page);
    for (int page = 0; page < 8; page++) {
        font_render_page(lines[page], page_buf[page]);
        esp_lcd_panel_draw_bitmap(s_panel, 0, page * 8, 128, page * 8 + 8, page_buf[page]);
    }
}
#endif // ifndef NODE_HOST_TEST
