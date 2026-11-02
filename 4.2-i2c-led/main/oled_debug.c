#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "oled_cmds.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DBG_STEP_MS           3000
#define DBG_FRAME_TIMEOUT_MS  1000
#define DBG_MAX_BUS_ERRORS    3
#define DBG_TRY_SH1106        0      // set to 1 if step 6 shows nothing

static const char *TAG = "oled-dbg";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static uint8_t s_fb[1 + 128 * OLED_HEIGHT / 8];   // control byte + framebuffer

static const uint8_t k_ref_init[] = {
    OLED_CTRL_CMD,
    0xAE,
    0xA8, OLED_HEIGHT - 1,
    0xD3, 0x00,
    0x40,
    0x20, 0x00,
    0xA1,
    0xC8,
    0xDA, (OLED_HEIGHT == 64) ? 0x12 : 0x02,
    0x81, 0x7F,
    0xD9, 0xF1,
    0xDB, 0x20,
    0xA4,
    0xA6,
    0x8D, 0x14,
    0xAF,
};

static void idle_forever(void)
{
    ESP_LOGW(TAG, "debug stopped here");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void look(const char *expect)
{
    ESP_LOGW(TAG, "    LOOK: %s", expect);
    vTaskDelay(pdMS_TO_TICKS(DBG_STEP_MS));
}

static bool send(const uint8_t *buf, size_t n, int timeout_ms, const char *what)
{
    esp_err_t err = i2c_master_transmit(s_dev, buf, n, timeout_ms);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "    %s: FAILED (%s)", what, esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "    %s: %u bytes ACKed", what, (unsigned)n);
    return true;
}

static bool cmd(uint8_t c, const char *what)
{
    const uint8_t b[2] = {OLED_CTRL_CMD, c};
    return send(b, sizeof(b), I2C_TIMEOUT_MS, what);
}

static bool push_frame(const char *what)
{
    const uint8_t win[] = {OLED_CTRL_CMD, 0x21, 0x00, 0x7F, 0x22, 0x00, OLED_HEIGHT / 8 - 1};
    if (!send(win, sizeof(win), I2C_TIMEOUT_MS, "set window")) {
        return false;
    }
    s_fb[0] = OLED_CTRL_DATA;
    return send(s_fb, sizeof(s_fb), DBG_FRAME_TIMEOUT_MS, what);
}

/* Weak internal pull-down: module pull-ups (4.7-10k) still win and read 1.
   No pull-up on the line -> reads 0 for sure. */
static void line_check(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << I2C_SDA_GPIO) | (1ULL << I2C_SCL_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    vTaskDelay(pdMS_TO_TICKS(5));
    int sda = gpio_get_level(I2C_SDA_GPIO);
    int scl = gpio_get_level(I2C_SCL_GPIO);
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io);

    ESP_LOGI(TAG, "[1] idle levels against weak pull-down: SDA=%d SCL=%d", sda, scl);
    if (sda && scl) {
        ESP_LOGI(TAG, "    OK: module pull-ups reach both lines");
        return;
    }
    if (!sda) {
        ESP_LOGE(TAG, "    SDA (GPIO%d) has no pull-up: wire/solder/module power", (int)I2C_SDA_GPIO);
    }
    if (!scl) {
        ESP_LOGE(TAG, "    SCL (GPIO%d) has no pull-up: wire/solder/module power", (int)I2C_SCL_GPIO);
    }
}

static void bus_init_dbg(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_bus));
    ESP_LOGI(TAG, "[2] bus up: SDA=GPIO%d SCL=GPIO%d %d Hz",
             (int)I2C_SDA_GPIO, (int)I2C_SCL_GPIO, I2C_HZ);
}

static uint8_t scan_for_oled(void)
{
    uint8_t oled = 0;
    int acks = 0;
    int errors = 0;

    ESP_LOGI(TAG, "[3] scan 0x%02X..0x%02X", ADDR_SCAN_FROM, ADDR_SCAN_TO);
    for (uint8_t a = ADDR_SCAN_FROM; a <= ADDR_SCAN_TO; a++) {
        esp_err_t err = i2c_master_probe(s_bus, a, I2C_TIMEOUT_MS);
        if (err == ESP_ERR_NOT_FOUND) {
            continue;
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "    0x%02X: %s -> bus reset", a, esp_err_to_name(err));
            i2c_master_bus_reset(s_bus);
            if (++errors >= DBG_MAX_BUS_ERRORS) {
                ESP_LOGE(TAG, "    bus keeps failing: lines held low or no pull-ups");
                break;
            }
            continue;
        }
        acks++;
        ESP_LOGI(TAG, "    ACK 0x%02X", a);
        if (oled == 0 && (a == ADDR_OLED || a == ADDR_OLED_ALT)) {
            oled = a;
        }
    }
    ESP_LOGI(TAG, "    %d device(s) answered", acks);
    return oled;
}

static void attach(uint8_t addr)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &cfg, &s_dev));
}

static void builder_check(void)
{
    oled_cfg_t cfg;
    oled_cfg_default(&cfg, OLED_HEIGHT);
    cfg.test_all_on = false;

    uint8_t buf[OLED_INIT_MAX];
    size_t n = oled_build_init(&cfg, buf, sizeof(buf));

    ESP_LOGI(TAG, "[4] oled_build_init: %u bytes (reference %u)",
             (unsigned)n, (unsigned)sizeof(k_ref_init));
    if (n == 0) {
        ESP_LOGE(TAG, "    builder rejected the default config");
        return;
    }
    ESP_LOG_BUFFER_HEX(TAG, buf, n);
    if (n != sizeof(k_ref_init)) {
        ESP_LOGE(TAG, "    length differs from reference");
        return;
    }
    for (size_t i = 0; i < n; i++) {
        if (buf[i] != k_ref_init[i]) {
            ESP_LOGE(TAG, "    first difference at [%u]: got 0x%02X, expected 0x%02X",
                     (unsigned)i, buf[i], k_ref_init[i]);
            return;
        }
    }
    ESP_LOGI(TAG, "    OK: identical to reference");
}

static void fill_clear(void)
{
    memset(&s_fb[1], 0x00, sizeof(s_fb) - 1);
}

static void fill_border_and_marker(void)
{
    const int pages = OLED_HEIGHT / 8;
    fill_clear();
    for (int x = 0; x < 128; x++) {
        s_fb[1 + x] |= 0x01;                          // top row
        s_fb[1 + (pages - 1) * 128 + x] |= 0x80;      // bottom row
    }
    for (int p = 0; p < pages; p++) {
        s_fb[1 + p * 128] = 0xFF;                     // left column
        s_fb[1 + p * 128 + 127] = 0xFF;               // right column
    }
    for (int x = 0; x < 8; x++) {
        s_fb[1 + x] = 0xFF;                           // 8x8 block, top-left
    }
}

static void fill_bands(void)
{
    const int pages = OLED_HEIGHT / 8;
    for (int p = 0; p < pages; p++) {
        memset(&s_fb[1 + p * 128], (p & 1) ? 0x00 : 0xFF, 128);
    }
}

void oled_debug_run(void)
{
    ESP_LOGI(TAG, "=== OLED debug, built %s %s, height %d ===", __DATE__, __TIME__, OLED_HEIGHT);
    vTaskDelay(pdMS_TO_TICKS(100));

    line_check();
    bus_init_dbg();

    uint8_t addr = scan_for_oled();
    if (addr == 0) {
        ESP_LOGE(TAG, "    no OLED at 0x%02X/0x%02X", ADDR_OLED, ADDR_OLED_ALT);
        idle_forever();
    }
    attach(addr);
    ESP_LOGI(TAG, "    using OLED at 0x%02X", addr);

    builder_check();

    ESP_LOGI(TAG, "[5] reference init");
    if (!send(k_ref_init, sizeof(k_ref_init), I2C_TIMEOUT_MS, "init")) {
        idle_forever();
    }
    look("random snow or black: panel powered and ON");

    ESP_LOGI(TAG, "[6] entire display ON (0xA5)");
    cmd(0xA5, "A5");
    look("EVERY pixel lit");

#if DBG_TRY_SH1106
    ESP_LOGI(TAG, "[6b] SH1106 charge pump (0xAD 0x8B) + ON + A5");
    const uint8_t sh[] = {OLED_CTRL_CMD, 0xAD, 0x8B, 0xAF, 0xA5};
    send(sh, sizeof(sh), I2C_TIMEOUT_MS, "SH1106 power-up");
    look("EVERY pixel lit (if it lights only now: controller is SH1106)");
#endif

    ESP_LOGI(TAG, "[7] back to RAM (0xA4) + clear");
    cmd(0xA4, "A4");
    fill_clear();
    push_frame("clear frame");
    look("completely black");

    ESP_LOGI(TAG, "[8] border + top-left marker");
    fill_border_and_marker();
    push_frame("border frame");
    look("1-px border, filled square in TOP-LEFT corner");

    ESP_LOGI(TAG, "[9] horizontal bands");
    fill_bands();
    push_frame("bands frame");
    look("8-px tall stripes, lit stripe at the top");

    ESP_LOGI(TAG, "[10] invert (0xA7)");
    cmd(0xA7, "A7");
    look("stripes swapped, dark stripe at the top");
    cmd(0xA6, "A6");

    ESP_LOGI(TAG, "[11] contrast sweep 0x00 -> 0xFF");
    for (int c = 0; c <= 0xFF; c += 0x11) {
        const uint8_t b[3] = {OLED_CTRL_CMD, 0x81, (uint8_t)c};
        i2c_master_transmit(s_dev, b, sizeof(b), I2C_TIMEOUT_MS);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    look("stripes went from dim to bright");

    fill_border_and_marker();
    push_frame("final frame");
    ESP_LOGI(TAG, "=== done, border left on screen ===");
    idle_forever();
}