#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oled_cmds.h"
#include "oled_debug.h"

static const char *TAG = "4.2-i2c";
static i2c_master_bus_handle_t s_bus;
static const char *addr_name(uint8_t addr)
{
    if (addr == ADDR_OLED || addr == ADDR_OLED_ALT)
    {
        return "OLED SSD1306";
    }
    if (addr == ADDR_RTC)
    {
        return "RTC DS1307";
    }
    return "";
}
static void bus_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        /* Внутрішня підтяжка — страховка. Rp уже на модулях; це не заміна зовнішніх. */
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_bus));
    ESP_LOGI(TAG, "bus SDA=GPIO%d SCL=GPIO%d %d Hz",
             (int)I2C_SDA_GPIO, (int)I2C_SCL_GPIO, I2C_HZ);
}
/* probe = START · ADDR+W · ACK/NACK · STOP, без байта даних.
ESP_OK — ACK (хтось відповів)
ESP_ERR_NOT_FOUND — NACK (порожня адреса, очікувано)
ESP_ERR_TIMEOUT — шина не рухається: обрив VCC/GND, не NACK */
static int scan_bus(uint8_t *found, int cap)
{
    int n = 0;
    ESP_LOGI(TAG, "scan 0x%02X..0x%02X", ADDR_SCAN_FROM, ADDR_SCAN_TO);
    for (uint8_t addr = ADDR_SCAN_FROM; addr <= ADDR_SCAN_TO; addr++)
    {
        esp_err_t err = i2c_master_probe(s_bus, addr, I2C_TIMEOUT_MS);
        if (err == ESP_ERR_TIMEOUT)
        {
            ESP_LOGE(TAG, "timeout 0x%02X — немає підтяжки або лінія замкнена", addr);
            break;
        }
        if (err != ESP_OK)
        {
            continue; /* NACK: цієї адреси на шині немає */
        }
        const char *name = addr_name(addr);
        if (name[0])
        {
            ESP_LOGI(TAG, " ACK 0x%02X %s", addr, name);
        }
        else
        {
            ESP_LOGI(TAG, " ACK 0x%02X", addr);
        }
        if (n < cap)
        {
            found[n++] = addr;
        }
    }
    ESP_LOGI(TAG, "found %d", n);
    if (n == 0)
    {
        ESP_LOGW(TAG, "порожньо: GND, 3V3 не 5V, SDA/SCL, шовк модуля");
    }
    return n;
}
static bool saw(const uint8_t *found, int n, uint8_t addr)
{
    for (int i = 0; i < n; i++)
    {
        if (found[i] == addr)
        {
            return true;
        }
    }
    return false;
}
/* Пристрій на шину лише після ACK у скані — інакше кожен обмін падатиме з NACK. */
static i2c_master_dev_handle_t attach(uint8_t addr)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, /* datasheet 0x68, не 0xD0 */
        .device_address = addr,
        .scl_speed_hz = I2C_HZ,
    };
    i2c_master_dev_handle_t dev = NULL;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &cfg, &dev));
    return dev;
}
static i2c_master_dev_handle_t attach_if_found(const uint8_t *found, int n, uint8_t addr)
{
    return saw(found, n, addr) ? attach(addr) : NULL;
}
/* DS1307 тримає календар у BCD: старший нібл — десятки, молодший — одиниці. */
static uint8_t bcd(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}
/* Запис номера регістра, повторний START, читання — кадр зі слайда. */
static esp_err_t rtc_read(i2c_master_dev_handle_t dev, uint8_t *buf, size_t n)
{
    uint8_t reg = 0x00; /* вказівник на секунди; далі автоінкремент */
    return i2c_master_transmit_receive(dev, &reg, 1, buf, n, I2C_TIMEOUT_MS);
}
static void rtc_unhalt(i2c_master_dev_handle_t dev)
{
    uint8_t b[1];
    if (rtc_read(dev, b, 1) != ESP_OK)
    {
        ESP_LOGE(TAG, "RTC: не читається");
        return;
    }
    if ((b[0] & 0x80) == 0)
    {
        return; /* CH уже 0 — генератор іде, не чіпати секунди */
    }
    /* CH=1 з заводу: генератор вимкнений. Знімаємо біт, секунди лишаємо. */
    uint8_t pkt[2] = {0x00, (uint8_t)(b[0] & 0x7F)};
    if (i2c_master_transmit(dev, pkt, sizeof(pkt), I2C_TIMEOUT_MS) == ESP_OK)
    {
        ESP_LOGW(TAG, "RTC: CH=1, генератор увімкнено");
    }
    else
    {
        ESP_LOGE(TAG, "RTC: не вдалося зняти CH");
    }
}
static void rtc_print(i2c_master_dev_handle_t dev)
{
    uint8_t b[7];
    if (rtc_read(dev, b, sizeof(b)) != ESP_OK)
    {
        ESP_LOGE(TAG, "RTC: читання не вдалося");
        return;
    }
    /* Біт 6 годин: 1 = 12-годинний режим (біти 4...0). Інакше 24-годинний (5...0). */
    int hour = bcd(b[2] & ((b[2] & 0x40) ? 0x1F : 0x3F));
    ESP_LOGI(TAG, "RTC 20%02d-%02d-%02d %02d:%02d:%02d CH=%d",
             bcd(b[6]),
             bcd(b[5] & 0x1F),
             bcd(b[4]),
             hour,
             bcd(b[1]),
             bcd(b[0] & 0x7F), /* маска: біт 7 — CH, не десятки секунд */
             (b[0] >> 7) & 1);
}
/*
 * Один пакет: Co=0, D/C=0, далі команди SSD1306.
 * 0x8D 0x14 — насос заряду (3,3 В). 0xDA 0x12 — COM для 128×64.
 * 0xA5 — усі пікселі, без кадрового буфера (тест шини, не графіка).
 */
static void oled_test(i2c_master_dev_handle_t dev, uint8_t addr)
{
    oled_cfg_t cfg;
    oled_cfg_default(&cfg, OLED_HEIGHT);
    cfg.test_all_on = true;

    uint8_t buf[OLED_INIT_MAX];
    size_t n = oled_build_init(&cfg, buf, sizeof(buf));
    if (n == 0) {
         ESP_LOGE(TAG, "bad config or buffer too small");
         return;
    }

    esp_err_t err = i2c_master_transmit(dev, buf, n, I2C_TIMEOUT_MS);

    if (err != ESP_OK) {
         ESP_LOGE(TAG, "OLED 0x%02X: init failed (%s)", addr, esp_err_to_name(err));
         return;
    }
    ESP_LOGI(TAG, "OLED 0x%02X: init sent, %d bytes, all pixels on", addr, (unsigned)n);

}
void app_main(void)
{
    #if OLED_DEBUG
    oled_debug_run();
    #endif
    /* normal code unchanged */
    uint8_t found[16];
    bus_init();
    int n = scan_bus(found, (int)(sizeof(found) / sizeof(found[0])));
    /* OLED і RTC чіпаємо лише якщо були в скані. */
    i2c_master_dev_handle_t rtc = attach_if_found(found, n, ADDR_RTC);
    uint8_t oled_addr = 0;
    if (saw(found, n, ADDR_OLED))
    {
        oled_addr = ADDR_OLED;
    }
    else if (saw(found, n, ADDR_OLED_ALT))
    {
        oled_addr = ADDR_OLED_ALT;
    }
    i2c_master_dev_handle_t oled = oled_addr ? attach(oled_addr) : NULL;
    if (oled)
    {
        oled_test(oled, oled_addr);
    }
    else
    {
        ESP_LOGW(TAG, "OLED 0x3C/0x3D немає в скані");
    }
    if (rtc)
    {
        rtc_unhalt(rtc); /* CH один раз на старті, не в циклі */
    }
    else
    {
        ESP_LOGW(TAG, "RTC 0x68 немає в скані");
    }
    for (;;)
    {
        if (rtc)
        {
            rtc_print(rtc);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}