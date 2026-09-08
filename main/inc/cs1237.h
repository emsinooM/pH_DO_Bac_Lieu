#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// Cấu hình chân GPIO mặc định
#define PH_DATA_PIN GPIO_NUM_17
#define PH_SCLK_PIN GPIO_NUM_18
#define TEMP_DATA_PIN GPIO_NUM_8
#define TEMP_SCLK_PIN GPIO_NUM_19

#define CS1237_CFG_40HZ_PGA1_CHA 0x50
#define CS1237_CMD_WRITE_REG 0x65

// Khởi tạo các chân GPIO cho hệ thống CS1237
void init_system_gpios(void);

// Cấu hình CS1237
void write_cs1237_config(gpio_num_t sclk_pin, gpio_num_t data_pin,
                         uint8_t config_val);

// Đọc giá trị ADC thô từ CS1237 (trả về true nếu thành công, false nếu timeout)
bool read_cs1237_raw(gpio_num_t sclk_pin, gpio_num_t data_pin, int32_t *out_raw);

uint8_t read_cs1237_config(gpio_num_t sclk_pin, gpio_num_t data_pin);

#ifdef __cplusplus
}
#endif
