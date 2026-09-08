#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_MB_PORT1_PIN_TX 40
#define CONFIG_MB_PORT1_PIN_RX 38
#define CONFIG_MB_PORT1_PIN_DE 39

#define CONFIG_MB_PORT2_PIN_TX 42
#define CONFIG_MB_PORT2_PIN_RX 41
#define CONFIG_MB_PORT2_PIN_DE 45

typedef enum {
    DO_SENSOR_FMT_INTEGER = 0,
    DO_SENSOR_FMT_FLOAT,
    DO_SENSOR_FMT_KOG206,
} do_sensor_format_t;

typedef enum {
    DO_SENSOR_RS485_UART_RTS = 0,
    DO_SENSOR_RS485_MANUAL_DE,
} do_sensor_rs485_mode_t;

typedef struct {
    uart_port_t uart_port;
    int pin_tx;
    int pin_rx;
    int pin_de;
    uint8_t slave_id;
    uint32_t baud_rate;
    do_sensor_format_t format;
    do_sensor_rs485_mode_t rs485_mode;
    bool swap_ab;
    bool debug;
    uint32_t poll_interval_ms;
    uint32_t modbus_timeout_ms;
    uint16_t task_stack;
    uint8_t task_priority;
    uart_parity_t parity;
    uart_stop_bits_t stop_bits;
} do_sensor_config_t;

typedef struct {
    float do_mg_l;
    float temp_c;
    float saturation_pct;
    bool valid;
    int error_code;
    uint32_t timestamp_ms;
} do_sensor_reading_t;

typedef void (*do_sensor_callback_t)(const do_sensor_reading_t *reading, void *user_ctx);

#define DO_SENSOR_CONFIG_DEFAULT() {                \
    .uart_port = UART_NUM_1,                        \
    .pin_tx = CONFIG_MB_PORT2_PIN_TX,               \
    .pin_rx = CONFIG_MB_PORT2_PIN_RX,               \
    .pin_de = CONFIG_MB_PORT2_PIN_DE,               \
    .slave_id = 5,                                  \
    .baud_rate = 9600,                              \
    .format = DO_SENSOR_FMT_KOG206,                 \
    .rs485_mode = DO_SENSOR_RS485_MANUAL_DE,        \
    .swap_ab = false,                               \
    .debug = false,                                 \
    .poll_interval_ms = 10000,                      \
    .modbus_timeout_ms = 1000,                      \
    .task_stack = 4096,                             \
    .task_priority = 5,                             \
    .parity = UART_PARITY_DISABLE,                  \
    .stop_bits = UART_STOP_BITS_1,                  \
}

extern uint8_t g_mb1_addr;
extern uint32_t g_mb1_baud;
extern uint8_t g_mb1_parity;
extern uint8_t g_mb1_stop;

extern uint8_t g_mb2_addr;
extern uint32_t g_mb2_baud;
extern uint8_t g_mb2_parity;
extern uint8_t g_mb2_stop;

void do_sensor_load_settings(void);
esp_err_t do_sensor_update_config(uint8_t slave_id, uint32_t baud_rate, uint8_t parity_val, uint8_t stop_val);
esp_err_t do_sensor_init(const do_sensor_config_t *config);
esp_err_t do_sensor_start(do_sensor_callback_t callback, void *user_ctx);
esp_err_t do_sensor_stop(void);
esp_err_t do_sensor_get_reading(do_sensor_reading_t *out);
bool do_sensor_is_running(void);
const char *do_sensor_error_str(int error_code);
esp_err_t do_sensor_probe(void);
esp_err_t do_sensor_dump_registers(void);
esp_err_t do_sensor_kog206_boot(void);
esp_err_t do_sensor_calibrate_zero(void);
esp_err_t do_sensor_calibrate_slope(void);
esp_err_t do_sensor_correct_temp(float standard_temp_c);
esp_err_t do_sensor_reset(void);
esp_err_t do_sensor_set_salinity(float salinity_psu);


#ifdef __cplusplus
}
#endif
