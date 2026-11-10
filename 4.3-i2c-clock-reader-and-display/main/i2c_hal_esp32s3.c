#include "i2c_hal.h"
#include "config.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"

#define MAX_DEV 4
#define ADDR7_MAX 0x7F

typedef struct
{
    uint8_t addr7;
    i2c_master_dev_handle_t dev;
} dev_slot_t;

static const char *TAG = "i2c_hal_esp32s3";
static i2c_master_bus_handle_t bus;
static dev_slot_t slots[MAX_DEV];
static size_t count;

static i2c_status_t map_err(esp_err_t esp_error)
{
    switch (esp_error)
    {
    case ESP_OK:
        return I2C_OK;
    case ESP_ERR_NOT_FOUND:
        return I2C_NACK;
    case ESP_ERR_INVALID_STATE:
        return I2C_NACK;
    case ESP_ERR_INVALID_RESPONSE:
        return I2C_NACK;
    case ESP_ERR_TIMEOUT:
        return I2C_TIMEOUT;
    default:
        return I2C_ERR;
    }
}

static i2c_master_dev_handle_t find(uint8_t addr7)
{
    for (size_t i = 0; i < count; i++)
    {
        if (slots[i].addr7 == addr7)
        {
            return slots[i].dev;
        }
    }

    return NULL;
}

bool i2c_hal_init(void)
{
    if (bus != NULL)
    {
        return true;
    }

    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = I2C_INTERNAL_PULLUP,
    };

    esp_err_t err = i2c_new_master_bus(&cfg, &bus);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "couldn't start new master buss");
        bus = NULL;
        return false;
    }

    return true;
}

i2c_status_t i2c_hal_probe(uint8_t addr7)
{
    if (bus == NULL || addr7 > ADDR7_MAX)
    {
        return I2C_ERR;
    }
    return map_err(i2c_master_probe(bus, addr7, I2C_TIMEOUT_MS));
}

bool i2c_hal_add(uint8_t addr7)
{
    if (bus == NULL || addr7 > ADDR7_MAX)
    {
        return false;
    }

    i2c_master_dev_handle_t dev = find(addr7);
    if (find(addr7) != NULL)
    {
        return true;
    }

    if (count >= MAX_DEV)
    {
        ESP_LOGW(TAG, "Table full");
        return false;
    }

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr7,
        .scl_speed_hz = I2C_HZ,
    };

    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &dev);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "add 0x%02X failed: %s", addr7, esp_err_to_name(err));
        return false;
    }

    dev_slot_t slot = {
        .addr7 = addr7,
        .dev = dev,
    }; 
    slots[count] = slot;

    count++;

    return true;
}

i2c_status_t i2c_hal_write(uint8_t addr7, const uint8_t *data, size_t len)
{
    i2c_master_dev_handle_t handle = find(addr7);

    if (handle == NULL || addr7 > ADDR7_MAX || data == NULL || len == 0)
    {
        return I2C_ERR;
    }

    return map_err(i2c_master_transmit(handle, data, len, I2C_TIMEOUT_MS));
}

i2c_status_t i2c_hal_write_read(uint8_t addr7, const uint8_t *wr, size_t wlen,
                                uint8_t *rd, size_t rlen) 
{
    i2c_master_dev_handle_t handle = find(addr7);

    if (handle == NULL || addr7 > ADDR7_MAX || wr == NULL || wlen == 0 || rd == NULL || rlen == 0) {
        ESP_LOGE(TAG, "Failure on attempt to execute i2c_hal_write_read  0x%02X failed", addr7);
        return I2C_ERR;
    }

    return map_err(i2c_master_transmit_receive(handle, wr, wlen, rd, rlen, I2C_TIMEOUT_MS));
}
