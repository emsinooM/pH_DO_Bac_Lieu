#include "ds3231.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#define I2C_MASTER_NUM             I2C_NUM_0
#define I2C_MASTER_FREQ_HZ         100000     // 100kHz

static const char *TAG = "DS3231";
static i2c_master_dev_handle_t ds3231_dev_handle = NULL;

static uint8_t dec_to_bcd(uint8_t val) {
    return ((val / 10) << 4) + (val % 10);
}

static uint8_t bcd_to_dec(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

esp_err_t ds3231_init(gpio_num_t sda_pin, gpio_num_t scl_pin) {
    if (ds3231_dev_handle != NULL) {
        ESP_LOGI(TAG, "DS3231 already initialized");
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus_handle;
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_I2C_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    err = i2c_master_bus_add_device(bus_handle, &dev_config, &ds3231_dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C initialized successfully on SDA:%d SCL:%d", sda_pin, scl_pin);
    return ESP_OK;
}

esp_err_t ds3231_get_time(struct tm *timeinfo) {
    if (timeinfo == NULL) return ESP_ERR_INVALID_ARG;
    if (ds3231_dev_handle == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t data[7];
    uint8_t reg = DS3231_REG_TIME;
    
    // Read 7 bytes starting from register 0x00 (seconds)
    esp_err_t err = i2c_master_transmit_receive(ds3231_dev_handle, &reg, 1, data, 7, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(err));
        return err;
    }

    timeinfo->tm_sec = bcd_to_dec(data[0] & 0x7F);
    timeinfo->tm_min = bcd_to_dec(data[1] & 0x7F);
    
    // Hour register: bit 6 is 12/24h mode.
    // If bit 6 is 0, it's 24-hour mode.
    // If bit 6 is 1, it's 12-hour mode.
    if (data[2] & 0x40) {
        // 12-hour mode
        uint8_t hour = bcd_to_dec(data[2] & 0x1F);
        if (data[2] & 0x20) { // PM flag
            if (hour < 12) hour += 12;
        } else { // AM flag
            if (hour == 12) hour = 0;
        }
        timeinfo->tm_hour = hour;
    } else {
        // 24-hour mode
        timeinfo->tm_hour = bcd_to_dec(data[2] & 0x3F);
    }

    timeinfo->tm_wday = bcd_to_dec(data[3] & 0x07) - 1; // DS3231 uses 1-7, struct tm uses 0-6
    timeinfo->tm_mday = bcd_to_dec(data[4] & 0x3F);
    timeinfo->tm_mon = bcd_to_dec(data[5] & 0x1F) - 1;   // DS3231 uses 1-12, struct tm uses 0-11
    timeinfo->tm_year = bcd_to_dec(data[6]) + 100;       // Years since 1900. Assume year in 2000-2099 (so 100 + yy)

    return ESP_OK;
}

esp_err_t ds3231_set_time(const struct tm *timeinfo) {
    if (timeinfo == NULL) return ESP_ERR_INVALID_ARG;
    if (ds3231_dev_handle == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t data[8];
    data[0] = DS3231_REG_TIME; // Start register address
    data[1] = dec_to_bcd(timeinfo->tm_sec);
    data[2] = dec_to_bcd(timeinfo->tm_min);
    data[3] = dec_to_bcd(timeinfo->tm_hour); // Write in 24-hour mode (bit 6 is 0)
    data[4] = dec_to_bcd(timeinfo->tm_wday + 1); // DS3231 uses 1-7, struct tm uses 0-6
    data[5] = dec_to_bcd(timeinfo->tm_mday);
    data[6] = dec_to_bcd(timeinfo->tm_mon + 1); // DS3231 uses 1-12, struct tm uses 0-11
    data[7] = dec_to_bcd(timeinfo->tm_year % 100);

    esp_err_t err = i2c_master_transmit(ds3231_dev_handle, data, 8, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Write failed: %s", esp_err_to_name(err));
        return err;
    }

    // Clear oscillator stop flag to mark time as valid
    return ds3231_clear_oscillator_flag();
}

esp_err_t ds3231_check_oscillator(bool *stopped) {
    if (stopped == NULL) return ESP_ERR_INVALID_ARG;
    if (ds3231_dev_handle == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t reg = DS3231_REG_STATUS;
    uint8_t val = 0;
    esp_err_t err = i2c_master_transmit_receive(ds3231_dev_handle, &reg, 1, &val, 1, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Check oscillator failed: %s", esp_err_to_name(err));
        return err;
    }

    *stopped = (val & DS3231_STATUS_OSF) != 0;
    return ESP_OK;
}

esp_err_t ds3231_clear_oscillator_flag(void) {
    if (ds3231_dev_handle == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t reg = DS3231_REG_STATUS;
    uint8_t val = 0;
    
    // Read status register
    esp_err_t err = i2c_master_transmit_receive(ds3231_dev_handle, &reg, 1, &val, 1, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read status failed: %s", esp_err_to_name(err));
        return err;
    }

    // Clear OSF (bit 7)
    val &= ~DS3231_STATUS_OSF;
    uint8_t data[2] = { DS3231_REG_STATUS, val };
    
    err = i2c_master_transmit(ds3231_dev_handle, data, 2, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Clear OSF failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Oscillator Stop Flag (OSF) cleared successfully.");
    return ESP_OK;
}
