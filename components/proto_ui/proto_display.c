#include "proto_display.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "proto_board.h"

#define OLED_PORT I2C_NUM_1
#define OLED_SDA PROTO_PIN_TFT_DC     /* GPIO14: reserved TFT D/C pin, free because no TFT is fitted */
#define OLED_SCL PROTO_PIN_TFT_CS     /* GPIO10: reserved TFT CS pin */
#define OLED_TIMEOUT_MS 100
#define OLED_CLOCK_HZ 400000

static const char *TAG = "display";
static proto_display_info_t s_info;
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

static esp_err_t oled_write(uint8_t control, const uint8_t *data, size_t length)
{
    uint8_t buffer[1 + PROTO_DISPLAY_WIDTH];
    if (length > PROTO_DISPLAY_WIDTH) { return ESP_ERR_INVALID_SIZE; }
    buffer[0] = control;                 /* 0x00 = commands, 0x40 = display data */
    memcpy(&buffer[1], data, length);
    return i2c_master_transmit(s_dev, buffer, length + 1, OLED_TIMEOUT_MS);
}

static esp_err_t oled_cmds(const uint8_t *cmds, size_t n)
{
    return oled_write(0x00, cmds, n);
}

void proto_display_stop(void)
{
    if (s_dev) { (void)i2c_master_bus_rm_device(s_dev); s_dev = NULL; }
    if (s_bus) { (void)i2c_del_master_bus(s_bus); s_bus = NULL; }
    s_info.active = false;
}

esp_err_t proto_display_flip(bool flipped)
{
    if (!s_info.active) { return ESP_ERR_INVALID_STATE; }
    const uint8_t cmds[] = { (uint8_t)(flipped ? 0xA0 : 0xA1), (uint8_t)(flipped ? 0xC0 : 0xC8) };
    const esp_err_t err = oled_cmds(cmds, sizeof(cmds));
    if (err == ESP_OK) { s_info.flipped = flipped; }
    return err;
}

esp_err_t proto_display_start(void)
{
    if (s_info.active) { return ESP_OK; }
    const uint32_t starts = s_info.starts, flushes = s_info.flushes, errors = s_info.flush_errors;
    memset(&s_info, 0, sizeof(s_info));
    s_info.starts = starts; s_info.flushes = flushes; s_info.flush_errors = errors;

    const i2c_master_bus_config_t bus = {
        .i2c_port = OLED_PORT,
        .sda_io_num = OLED_SDA,
        .scl_io_num = OLED_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus, &s_bus);
    if (err != ESP_OK) { return err; }
    uint8_t address = 0;
    if (i2c_master_probe(s_bus, 0x3C, 50) == ESP_OK) { address = 0x3C; }
    else if (i2c_master_probe(s_bus, 0x3D, 50) == ESP_OK) { address = 0x3D; }
    if (!address) {
        proto_display_stop();
        return ESP_ERR_NOT_FOUND;
    }
    const i2c_device_config_t dev = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = address,
                                      .scl_speed_hz = OLED_CLOCK_HZ };
    err = i2c_master_bus_add_device(s_bus, &dev, &s_dev);
    if (err != ESP_OK) { proto_display_stop(); return err; }
    static const uint8_t init[] = {
        0xAE,             /* display off */
        0xD5, 0x80,       /* clock divide / oscillator */
        0xA8, 0x3F,       /* multiplex 1/64 */
        0xD3, 0x00,       /* display offset */
        0x40,             /* start line 0 */
        0x8D, 0x14,       /* charge pump on */
        0x20, 0x00,       /* horizontal addressing */
        0xA1, 0xC8,       /* segment remap, COM scan direction: normal orientation */
        0xDA, 0x12,       /* COM pins for 128x64 */
        0x81, 0xCF,       /* contrast */
        0xD9, 0xF1,       /* pre-charge */
        0xDB, 0x40,       /* VCOMH */
        0xA4, 0xA6,       /* resume RAM content, non-inverted */
        0xAF,             /* display on */
    };
    err = oled_cmds(init, sizeof(init));
    if (err != ESP_OK) { proto_display_stop(); return err; }
    s_info.active = true;
    s_info.name = "SSD1306";
    s_info.i2c_address = address;
    s_info.width = PROTO_DISPLAY_WIDTH;
    s_info.height = PROTO_DISPLAY_HEIGHT;
    s_info.starts++;
#ifdef CONFIG_PROTO_DISPLAY_OLED_FLIP
    (void)proto_display_flip(true);
#endif
    ESP_LOGI(TAG, "OLED SSD1306 128x64 found at 0x%02X (SDA GPIO%d, SCL GPIO%d)", address, OLED_SDA, OLED_SCL);
    return ESP_OK;
}

proto_display_info_t proto_display_info(void)
{
    return s_info;
}

esp_err_t proto_display_flush(const proto_canvas_t *canvas)
{
    if (!s_info.active || !canvas || !canvas->pixels || canvas->width != PROTO_DISPLAY_WIDTH ||
        canvas->height != PROTO_DISPLAY_HEIGHT) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t page_bytes[PROTO_DISPLAY_WIDTH];
    esp_err_t err = ESP_OK;
    for (uint8_t page = 0; page < PROTO_DISPLAY_HEIGHT / 8 && err == ESP_OK; ++page) {
        const uint8_t addr[] = { (uint8_t)(0xB0 | page), 0x00, 0x10 };   /* page, column 0 */
        err = oled_cmds(addr, sizeof(addr));
        if (err != ESP_OK) { break; }
        proto_canvas_pack_page(canvas, page, page_bytes);
        err = oled_write(0x40, page_bytes, PROTO_DISPLAY_WIDTH);
    }
    if (err == ESP_OK) {
        s_info.flushes++;
        s_info.consecutive_errors = 0;
    } else {
        s_info.flush_errors++;
        s_info.consecutive_errors++;
    }
    return err;
}
