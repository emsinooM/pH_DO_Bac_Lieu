#pragma once

#include <time.h>
#include "esp_err.h"
#include "driver/gpio.h"

#define DS3231_I2C_ADDR         0x68
#define DS3231_REG_TIME         0x00
#define DS3231_REG_STATUS       0x0F
#define DS3231_STATUS_OSF       0x80

/**
 * @brief Initialize I2C driver and configure for DS3231
 * 
 * @param sda_pin GPIO pin for SDA
 * @param scl_pin GPIO pin for SCL
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ds3231_init(gpio_num_t sda_pin, gpio_num_t scl_pin);

/**
 * @brief Read date and time from DS3231
 * 
 * @param timeinfo Pointer to struct tm where time will be stored
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ds3231_get_time(struct tm *timeinfo);

/**
 * @brief Write date and time to DS3231
 * 
 * @param timeinfo Pointer to struct tm containing the time to write
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ds3231_set_time(const struct tm *timeinfo);

/**
 * @brief Check if the DS3231 oscillator has stopped (indicating power loss)
 * 
 * @param stopped Pointer to bool to store the status
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ds3231_check_oscillator(bool *stopped);

/**
 * @brief Clear the OSF (Oscillator Stop Flag) in the status register
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ds3231_clear_oscillator_flag(void);
