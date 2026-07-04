#include "do_sensor.h"
#include "esp_err.h"
#include "ph_temp.h"
#include "esp_rom_sys.h"
#include "user_storage.h"

#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "portmacro.h"
#include "projdefs.h"

static const char *TAG = "do_sensor";

uint8_t g_mb1_addr = 1;
uint32_t g_mb1_baud = 9600;
uint8_t g_mb1_parity = 0; // 0=None, 1=Even, 2=Odd
uint8_t g_mb1_stop = 1;

uint8_t g_mb2_addr = 5;
uint32_t g_mb2_baud = 9600;
uint8_t g_mb2_parity = 0;
uint8_t g_mb2_stop = 1;

void do_sensor_load_settings(void) {
    uint32_t val;
    if (Nvs_Read_Number("mb1_addr", &val)) g_mb1_addr = (uint8_t)val;
    if (Nvs_Read_Number("mb1_baud", &val)) g_mb1_baud = val;
    if (Nvs_Read_Number("mb1_parity", &val)) g_mb1_parity = (uint8_t)val;
    if (Nvs_Read_Number("mb1_stop", &val)) g_mb1_stop = (uint8_t)val;

    if (Nvs_Read_Number("mb2_addr", &val)) g_mb2_addr = (uint8_t)val;
    if (Nvs_Read_Number("mb2_baud", &val)) g_mb2_baud = val;
    if (Nvs_Read_Number("mb2_parity", &val)) g_mb2_parity = (uint8_t)val;
    if (Nvs_Read_Number("mb2_stop", &val)) g_mb2_stop = (uint8_t)val;

    ESP_LOGI("DO_SENSOR", "Loaded MB Port 1: Addr=%d, Baud=%lu, Parity=%d, Stop=%d",
             g_mb1_addr, (unsigned long)g_mb1_baud, g_mb1_parity, g_mb1_stop);
    ESP_LOGI("DO_SENSOR", "Loaded MB Port 2: Addr=%d, Baud=%lu, Parity=%d, Stop=%d",
             g_mb2_addr, (unsigned long)g_mb2_baud, g_mb2_parity, g_mb2_stop);
}

static do_sensor_config_t s_cfg;
static do_sensor_reading_t s_latest;
static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task_handle;
static do_sensor_callback_t s_callback;
static void *s_user_ctx;
static bool s_initialized;

static uint16_t modbus_crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;

  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

static void log_hex_line(const char *label, const uint8_t *data, int len,
                         bool enable) {
  if (!enable || len <= 0) {
    return;
  }

  char buf[256];
  int pos = 0;
  for (int i = 0; i < len && pos < (int)sizeof(buf) - 4; i++) {
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%02X ", data[i]);
  }
  ESP_LOGI(TAG, "%s (%d): %s", label, len, buf);
}

static void rs485_set_tx(bool tx) {
  if (s_cfg.rs485_mode == DO_SENSOR_RS485_MANUAL_DE) {
    gpio_set_level(s_cfg.pin_de, tx ? 1 : 0);
  }
}

static esp_err_t rs485_gpio_init(void) {
  if (s_cfg.rs485_mode != DO_SENSOR_RS485_MANUAL_DE) {
    return ESP_OK;
  }

  gpio_config_t io = {
      .pin_bit_mask = 1ULL << s_cfg.pin_de,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio_config failed");
  rs485_set_tx(false);
  return ESP_OK;
}

static esp_err_t uart_rs485_init(void) {
  uart_config_t cfg = {
      .baud_rate = (int)s_cfg.baud_rate,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  // Cấu hình TX buffer size = 0 để ghi trực tiếp vào phần cứng FIFO, tránh lỗi ngắt khi chạy cross-core
  ESP_RETURN_ON_ERROR(
      uart_driver_install(s_cfg.uart_port, 1024, 0, 0, NULL, 0), TAG,
      "uart_driver_install failed");
  ESP_RETURN_ON_ERROR(uart_param_config(s_cfg.uart_port, &cfg), TAG,
                      "uart_param_config failed");

  if (s_cfg.rs485_mode == DO_SENSOR_RS485_UART_RTS) {
    ESP_RETURN_ON_ERROR(uart_set_pin(s_cfg.uart_port, s_cfg.pin_tx,
                                     s_cfg.pin_rx, s_cfg.pin_de,
                                     UART_PIN_NO_CHANGE),
                        TAG, "uart_set_pin failed");
    ESP_RETURN_ON_ERROR(
        uart_set_mode(s_cfg.uart_port, UART_MODE_RS485_HALF_DUPLEX), TAG,
        "uart_set_mode failed");
    ESP_RETURN_ON_ERROR(
        uart_set_line_inverse(s_cfg.uart_port, UART_SIGNAL_RTS_INV), TAG,
        "uart_set_line_inverse failed");
  } else {
    ESP_RETURN_ON_ERROR(uart_set_pin(s_cfg.uart_port, s_cfg.pin_tx,
                                     s_cfg.pin_rx, UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE),
                        TAG, "uart_set_pin failed");
    ESP_RETURN_ON_ERROR(uart_set_mode(s_cfg.uart_port, UART_MODE_UART), TAG,
                        "uart_set_mode failed");
    ESP_RETURN_ON_ERROR(rs485_gpio_init(), TAG, "rs485 gpio failed");
  }

  return ESP_OK;
}

static int modbus_read_regs(uint8_t slave, uint8_t function, uint16_t start_reg,
                            uint16_t count, uint16_t *out_regs) {
  uint8_t req[8];

  req[0] = slave;
  req[1] = function;
  req[2] = (uint8_t)(start_reg >> 8);
  req[3] = (uint8_t)(start_reg & 0xFF);
  req[4] = (uint8_t)(count >> 8);
  req[5] = (uint8_t)(count & 0xFF);

  uint16_t crc = modbus_crc16(req, 6);
  req[6] = (uint8_t)(crc & 0xFF);
  req[7] = (uint8_t)(crc >> 8);

  uart_flush_input(s_cfg.uart_port);
  log_hex_line("TX", req, sizeof(req), s_cfg.debug);

  rs485_set_tx(true);
  if (uart_write_bytes(s_cfg.uart_port, (const char *)req, sizeof(req)) !=
      (int)sizeof(req)) {
    rs485_set_tx(false);
    return -1;
  }
  // Đợi truyền xong (chờ bằng ngắt và dự phòng bằng delay cứng tránh lỗi cross-core)
  esp_err_t tx_err = uart_wait_tx_done(s_cfg.uart_port, pdMS_TO_TICKS(100));
  if (tx_err != ESP_OK) {
    // Dự phòng bằng delay cứng (busy-wait) nếu uart_wait_tx_done lỗi hoặc timeout
    uint32_t tx_delay_us = (sizeof(req) * 10 * 1000000) / s_cfg.baud_rate + 2000;
    esp_rom_delay_us(tx_delay_us);
  } else {
    esp_rom_delay_us(1000); // Delay ổn định dòng trước khi ngắt DE
  }
  rs485_set_tx(false);
  vTaskDelay(pdMS_TO_TICKS(5));

  uint8_t resp[256];
  int total = 0;
  TickType_t deadline =
      xTaskGetTickCount() + pdMS_TO_TICKS(s_cfg.modbus_timeout_ms);

  while (xTaskGetTickCount() < deadline) {
    int n = uart_read_bytes(s_cfg.uart_port, resp + total, sizeof(resp) - total,
                            pdMS_TO_TICKS(500));
    if (n <= 0) {
      continue;
    }

    total += n;
    if (total >= 3 && (resp[1] & 0x80)) {
      break;
    }
    if (total >= 5) {
      int expected = 3 + resp[2] + 2;
      if (total >= expected) {
        break;
      }
    }
  }

  log_hex_line("RX", resp, total, s_cfg.debug);

  if (total < 5) {
    return -2;
  }

  if (resp[0] != slave || resp[1] != function) {
    if (resp[1] & 0x80) {
      return resp[2];
    }
    return -3;
  }

  uint16_t rx_crc = resp[total - 2] | ((uint16_t)resp[total - 1] << 8);
  if (modbus_crc16(resp, total - 2) != rx_crc) {
    return -4;
  }

  if (resp[2] != count * 2) {
    return -5;
  }

  for (int i = 0; i < count; i++) {
    out_regs[i] = ((uint16_t)resp[3 + i * 2] << 8) | resp[4 + i * 2];
  }

  return 0;
}

static float regs_to_float(uint16_t hi, uint16_t lo) {
  uint32_t raw = ((uint32_t)hi << 16) | lo;
  float value;
  memcpy(&value, &raw, sizeof(value));
  return value;
}

static float scale_by_decimals(uint16_t raw, uint16_t decimals) {
  if (decimals > 4) {
    decimals = 4;
  }
  float div = 1.0f;
  for (uint16_t i = 0; i < decimals; i++) {
    div *= 10.0f;
  }
  return raw / div;
}

static int modbus_write_reg(uint8_t slave, uint16_t reg, uint16_t value) {
  uint8_t req[8];

  req[0] = slave;
  req[1] = 0x06;
  req[2] = (uint8_t)(reg >> 8);
  req[3] = (uint8_t)(reg & 0xFF);
  req[4] = (uint8_t)(value >> 8);
  req[5] = (uint8_t)(value & 0xFF);

  uint16_t crc = modbus_crc16(req, 6);
  req[6] = (uint8_t)(crc & 0xFF);
  req[7] = (uint8_t)(crc >> 8);

  int retries = 3;
  int last_err = 0;

  for (int attempt = 0; attempt < retries; attempt++) {
    // 1. Guard delay before sending (50ms) to ensure bus quietness and sensor readiness
    vTaskDelay(pdMS_TO_TICKS(50));

    if (attempt > 0) {
      ESP_LOGW(TAG, "Ghi Modbus that bai (%d), dang thu lai lan %d/%d...", last_err, attempt + 1, retries);
    }

    uart_flush_input(s_cfg.uart_port);
    log_hex_line("TX", req, sizeof(req), s_cfg.debug);

    rs485_set_tx(true);
    if (uart_write_bytes(s_cfg.uart_port, (const char *)req, sizeof(req)) !=
        (int)sizeof(req)) {
      rs485_set_tx(false);
      last_err = -1;
      continue;
    }
    // Đợi truyền xong (chờ bằng ngắt và dự phòng bằng delay cứng tránh lỗi cross-core)
    esp_err_t tx_err = uart_wait_tx_done(s_cfg.uart_port, pdMS_TO_TICKS(100));
    if (tx_err != ESP_OK) {
      // Dự phòng bằng delay cứng (busy-wait) nếu uart_wait_tx_done lỗi hoặc timeout
      uint32_t tx_delay_us = (sizeof(req) * 10 * 1000000) / s_cfg.baud_rate + 2000;
      esp_rom_delay_us(tx_delay_us);
    } else {
      esp_rom_delay_us(1000); // Delay ổn định dòng trước khi ngắt DE
    }
    rs485_set_tx(false);
    vTaskDelay(pdMS_TO_TICKS(5));

    uint8_t resp[8];
    int total = 0;
    // Keep timeout at 5000ms as originally defined, but checked in a loop to prevent early return.
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(5000);

    while (xTaskGetTickCount() < deadline) {
      int to_read = sizeof(resp) - total;
      int n = uart_read_bytes(s_cfg.uart_port, resp + total, to_read, pdMS_TO_TICKS(100));
      if (n <= 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
      total += n;

      // Check exception response (5 bytes minimum)
      if (total >= 3 && (resp[1] & 0x80)) {
        if (total >= 5) {
          break;
        }
      }
      // Check normal response (8 bytes)
      if (total >= 8) {
        break;
      }
    }

    log_hex_line("RX", resp, total, s_cfg.debug);

    if (total < 5) {
      last_err = -2; // Timeout
      continue;
    }

    if (resp[0] != slave) {
      last_err = -3; // Invalid frame (wrong slave ID)
      continue;
    }

    if (resp[1] & 0x80) {
      // Exception response: return the exception code directly without retrying
      // Exception means slave received the command but rejected it, so retrying won't help.
      vTaskDelay(pdMS_TO_TICKS(100));
      return resp[2];
    }

    if (resp[1] != 0x06) {
      last_err = -3; // Invalid function code
      continue;
    }

    if (total < 8) {
      last_err = -2; // Incomplete frame
      continue;
    }

    // Verify CRC
    uint16_t rx_crc = resp[6] | ((uint16_t)resp[7] << 8);
    if (modbus_crc16(resp, 6) != rx_crc) {
      last_err = -4; // CRC error
      continue;
    }

    // Success! Add guard delay (100ms) before returning to let the sensor settle after EEPROM write
    vTaskDelay(pdMS_TO_TICKS(100));
    return 0;
  }

  // If we ran out of retries, add final delay and return last error
  vTaskDelay(pdMS_TO_TICKS(100));
  return last_err;
}

static bool modbus_read_block(uint16_t start, uint16_t count, uint16_t *regs,
                              int *err) {
  *err = modbus_read_regs(s_cfg.slave_id, 0x03, start, count, regs);
  if (*err != 0) {
    *err = modbus_read_regs(s_cfg.slave_id, 0x04, start, count, regs);
  }
  return *err == 0;
}

static bool modbus_read_one(uint16_t addr, uint16_t *val, int *err) {
  *err = modbus_read_regs(s_cfg.slave_id, 0x03, addr, 1, val);
  if (*err != 0) {
    *err = modbus_read_regs(s_cfg.slave_id, 0x04, addr, 1, val);
  }
  return *err == 0;
}

static bool read_kog206_meas(uint16_t meas[4], int *err) {
  if (modbus_read_block(0x0000, 4, meas, err)) {
    return true;
  }

  /* fallback: một số firmware chỉ cho đọc 1 reg/lần */
  ESP_LOGW(TAG, "doc block 0x0000 x4 failed (%d), doc tung reg", *err);
  for (int i = 0; i < 4; i++) {
    if (!modbus_read_one(0x0000 + i, &meas[i], err)) {
      return false;
    }
  }
  return true;
}

static bool read_kog206(float *do_mg_l, float *temp_c, float *sat_pct,
                        int *err) {
  uint16_t meas[4] = {0};

  /* Manual KOG-206: đọc 4 reg từ 0x0000 (40001)
   *   [0] giá trị DO, [1] số chữ số thập phân DO (vd 2 → ÷100)
   *   [2] giá trị nhiệt, [3] số chữ số thập phân nhiệt (vd 1 → ÷10)
   * Ví dụ manual: DO 0x0102 dec=2 → 2.58 mg/L; temp 0x00B0 dec=1 → 17.6°C
   */
  if (!read_kog206_meas(meas, err)) {
    return false;
  }

  *do_mg_l = scale_by_decimals(meas[0], meas[1]);
  *temp_c = scale_by_decimals(meas[2], meas[3]);

  uint16_t sat[2] = {0};
  int se = 0;
  if (modbus_read_block(0x0004, 2, sat, &se)) {
    *sat_pct = scale_by_decimals(sat[0], sat[1]);
  } else if (modbus_read_one(0x0004, &sat[0], &se) &&
             modbus_read_one(0x0005, &sat[1], &se)) {
    *sat_pct = scale_by_decimals(sat[0], sat[1]);
  } else {
    *sat_pct = 0.0f;
  }

  if (s_cfg.debug) {
    ESP_LOGI(TAG,
             "read raw DO=%u dec=%u -> %.2f mg/L | temp=%u dec=%u -> %.1f C | "
             "sat=%u dec=%u -> %.1f%%",
             meas[0], meas[1], *do_mg_l, meas[2], meas[3], *temp_c, sat[0],
             sat[1], *sat_pct);
  }

  *err = 0;
  return true;
}

esp_err_t do_sensor_kog206_boot(void) {
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  int e = modbus_write_reg(s_cfg.slave_id, 0x1100, 1);
  if (e != 0) {
    ESP_LOGW(TAG, "boot 0x1100 failed: %d (co the da bat san)", e);
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "boot OK: reg 0x1100=1 (bat do)");
  return ESP_OK;
}

static bool read_sensor_once(float *do_mg_l, float *temp_c, float *sat_pct,
                             int *err) {
  uint16_t regs[6] = {0};
  int e;

  if (s_cfg.format == DO_SENSOR_FMT_KOG206) {
    return read_kog206(do_mg_l, temp_c, sat_pct, err);
  }

  if (s_cfg.format == DO_SENSOR_FMT_INTEGER) {
    e = modbus_read_regs(s_cfg.slave_id, 0x03, 0x0001, 2, regs);
    if (e != 0) {
      e = modbus_read_regs(s_cfg.slave_id, 0x04, 0x0001, 2, regs);
    }
    if (e != 0) {
      *err = e;
      return false;
    }
    *sat_pct = 0.0f;
    *temp_c = regs[0] / 10.0f;
    *do_mg_l = regs[1] / 100.0f;
    *err = 0;
    return true;
  }

  e = modbus_read_regs(s_cfg.slave_id, 0x03, 0x0000, 6, regs);
  if (e != 0) {
    e = modbus_read_regs(s_cfg.slave_id, 0x04, 0x0000, 6, regs);
  }
  if (e != 0) {
    *err = e;
    return false;
  }
  *sat_pct = regs_to_float(regs[0], regs[1]) * 100.0f;
  *do_mg_l = regs_to_float(regs[2], regs[3]);
  *temp_c = regs_to_float(regs[4], regs[5]);
  *err = 0;
  return true;
}

static void publish_reading(const do_sensor_reading_t *reading) {
  if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
    s_latest = *reading;
    xSemaphoreGive(s_mutex);
  }

  // Cập nhật giá trị DO vào trạng thái cảm biến toàn cục
  Update_DO_Sensor_Measurements(reading->do_mg_l, reading->temp_c,
                                reading->saturation_pct, reading->valid,
                                reading->error_code);

  if (s_callback != NULL) {
    s_callback(reading, s_user_ctx);
  }
}

static void do_sensor_task(void *arg) {
  (void)arg;

  ESP_LOGI(TAG, "task started: id=%u baud=%" PRIu32 " de=GPIO%d mode=%s",
           s_cfg.slave_id, s_cfg.baud_rate, s_cfg.pin_de,
           s_cfg.rs485_mode == DO_SENSOR_RS485_MANUAL_DE ? "manual"
                                                         : "uart_rts");

  do_sensor_probe();
  do_sensor_kog206_boot();

  while (true) {
    do_sensor_reading_t reading = {
        .do_mg_l = 0.0f,
        .temp_c = 0.0f,
        .saturation_pct = 0.0f,
        .valid = false,
        .error_code = 0,
        .timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
    };

    int err = 0;
    bool read_ok = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
      read_ok = read_sensor_once(&reading.do_mg_l, &reading.temp_c,
                                 &reading.saturation_pct, &err);
      xSemaphoreGive(s_mutex);
    } else {
      err = -1;
    }

    if (read_ok) {
      reading.valid = true;
    } else {
      reading.error_code = err;
      ESP_LOGW(TAG, "read failed: %s (%d)", do_sensor_error_str(err), err);
      if (err == -2) {
        ESP_LOGW(TAG,
                 "timeout -> kiem tra: nguon 12-24V, GND chung, doi day A/B, "
                 "DE->GPIO%d",
                 s_cfg.pin_de);
      }
    }

    publish_reading(&reading);
    vTaskDelay(pdMS_TO_TICKS(s_cfg.poll_interval_ms));
  }
}

const char *do_sensor_error_str(int error_code) {
  switch (error_code) {
  case -1:
    return "uart write failed";
  case -2:
    return "timeout";
  case -3:
    return "invalid frame";
  case -4:
    return "crc error";
  case -5:
    return "invalid data length";
  case 0x01:
    return "illegal function";
  case 0x02:
    return "illegal data address";
  case 0x03:
    return "illegal data value";
  case 0x04:
    return "slave device failure";
  default:
    return "unknown error";
  }
}

esp_err_t do_sensor_probe(void) {
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  static const uint32_t baud_list[] = {9600, 4800, 19200, 2400};
  ESP_LOGI(TAG, "=== probe KOG-206: fc03 reg 0x0000 x4 ===");

  for (size_t b = 0; b < sizeof(baud_list) / sizeof(baud_list[0]); b++) {
    uart_set_baudrate(s_cfg.uart_port, baud_list[b]);

    for (uint8_t id = 1; id <= 10; id++) {
      uint16_t regs[4] = {0};
      int e = modbus_read_regs(id, 0x03, 0x0000, 4, regs);
      if (e != 0) {
        /* fallback doc tung reg */
        int ok = 1;
        for (int i = 0; i < 4 && ok; i++) {
          int re = modbus_read_regs(id, 0x03, 0x0000 + i, 1, &regs[i]);
          if (re != 0) {
            ok = 0;
          }
        }
        if (!ok) {
          if (e > 0) {
            ESP_LOGI(TAG, "RESPONSE id=%u baud=%" PRIu32 " exception=0x%02X",
                     id, baud_list[b], e);
          }
          continue;
        }
      }
      {
        s_cfg.slave_id = id;
        s_cfg.baud_rate = baud_list[b];
        uart_set_baudrate(s_cfg.uart_port, s_cfg.baud_rate);
        ESP_LOGI(TAG, "FOUND: id=%u baud=%" PRIu32 " DO=%.2f mg/L temp=%.1f C",
                 id, baud_list[b], scale_by_decimals(regs[0], regs[1]),
                 scale_by_decimals(regs[2], regs[3]));
        ESP_LOGI(TAG, "auto-applied slave_id=%u baud=%" PRIu32, s_cfg.slave_id,
                 s_cfg.baud_rate);
        return ESP_OK;
      }
    }
  }

  uart_set_baudrate(s_cfg.uart_port, s_cfg.baud_rate);
  ESP_LOGW(TAG, "probe: khong tim thay sensor");
  return ESP_ERR_NOT_FOUND;
}

esp_err_t do_sensor_dump_registers(void) {
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "=== KOG-206 reg map (manual) id=%u ===", s_cfg.slave_id);

  uint16_t meas[4] = {0};
  int e = 0;
  if (read_kog206_meas(meas, &e)) {
    ESP_LOGI(TAG, "0x0000 DO raw=%u dec=%u -> %.2f mg/L", meas[0], meas[1],
             scale_by_decimals(meas[0], meas[1]));
    ESP_LOGI(TAG, "0x0002 temp raw=%u dec=%u -> %.1f C", meas[2], meas[3],
             scale_by_decimals(meas[2], meas[3]));
  } else {
    ESP_LOGW(TAG, "doc 0x0000-0x0003 failed: %d", e);
  }

  uint16_t sat[2] = {0};
  if (modbus_read_block(0x0004, 2, sat, &e)) {
    ESP_LOGI(TAG, "0x0004 sat raw=%u dec=%u -> %.1f%%", sat[0], sat[1],
             scale_by_decimals(sat[0], sat[1]));
  }

  return ESP_OK;
}

esp_err_t do_sensor_init(const do_sensor_config_t *config) {
  if (config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  s_cfg = *config;
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) {
    return ESP_ERR_NO_MEM;
  }

  memset(&s_latest, 0, sizeof(s_latest));
  
  if (s_cfg.parity != UART_PARITY_DISABLE && s_cfg.parity != UART_PARITY_EVEN && s_cfg.parity != UART_PARITY_ODD) {
      s_cfg.parity = UART_PARITY_DISABLE;
  }
  if (s_cfg.stop_bits != UART_STOP_BITS_1 && s_cfg.stop_bits != UART_STOP_BITS_2) {
      s_cfg.stop_bits = UART_STOP_BITS_1;
  }

  ESP_RETURN_ON_ERROR(uart_rs485_init(), TAG, "rs485 init failed");

  s_initialized = true;
  ESP_LOGI(TAG, "init OK: TX=%d RX=%d DE=%d baud=%lu parity=%d stop=%d", s_cfg.pin_tx, s_cfg.pin_rx,
           s_cfg.pin_de, (unsigned long)s_cfg.baud_rate, (int)s_cfg.parity, (int)s_cfg.stop_bits);
  return ESP_OK;
}

esp_err_t do_sensor_update_config(uint8_t slave_id, uint32_t baud_rate, uint8_t parity_val, uint8_t stop_val) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    
    s_cfg.slave_id = slave_id;
    s_cfg.baud_rate = baud_rate;
    
    uart_parity_t uart_parity = UART_PARITY_DISABLE;
    if (parity_val == 1) uart_parity = UART_PARITY_EVEN;
    else if (parity_val == 2) uart_parity = UART_PARITY_ODD;
    s_cfg.parity = uart_parity;
    
    uart_stop_bits_t uart_stop = UART_STOP_BITS_1;
    if (stop_val == 2) uart_stop = UART_STOP_BITS_2;
    s_cfg.stop_bits = uart_stop;
    
    uart_set_baudrate(s_cfg.uart_port, baud_rate);
    uart_set_parity(s_cfg.uart_port, uart_parity);
    uart_set_stop_bits(s_cfg.uart_port, uart_stop);
    
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Config updated: slave_id=%d, baud=%lu, parity=%d, stop_bits=%d",
             slave_id, (unsigned long)baud_rate, (int)uart_parity, (int)uart_stop);
             
    return ESP_OK;
}

esp_err_t do_sensor_start(do_sensor_callback_t callback, void *user_ctx) {
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_task_handle != NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  s_callback = callback;
  s_user_ctx = user_ctx;

  BaseType_t ok = xTaskCreate(do_sensor_task, "do_sensor", s_cfg.task_stack,
                              NULL, s_cfg.task_priority, &s_task_handle);
  if (ok != pdPASS) {
    s_task_handle = NULL;
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

esp_err_t do_sensor_stop(void) {
  if (s_task_handle == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  vTaskDelete(s_task_handle);
  s_task_handle = NULL;
  s_callback = NULL;
  s_user_ctx = NULL;
  return ESP_OK;
}

esp_err_t do_sensor_get_reading(do_sensor_reading_t *out) {
  if (out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
    return ESP_FAIL;
  }
  *out = s_latest;
  xSemaphoreGive(s_mutex);
  return ESP_OK;
}

bool do_sensor_is_running(void) { return s_task_handle != NULL; }


esp_err_t do_sensor_calibrate_zero(void){
    if(!s_initialized){
        ESP_LOGE(TAG, "Lỗi: Cảm biến DO chưa được khởi tạo!");
        return ESP_ERR_INVALID_STATE;
    }
    // Bảo vệ tài nguyên UART bằng Mutex để tránh xung đột với task đọc cảm biến định kỳ
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE){
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Bắt đầu gửi lệnh hiệu chuẩn điểm 0 (Zero Calibration) đến DO Sensor (Slave: %d)", s_cfg.slave_id);

    int err = modbus_write_reg(s_cfg.slave_id, 0x1000, 0x0000);
    xSemaphoreGive(s_mutex);

    if(err != 0){
        ESP_LOGE(TAG, "Lưu hiệu chuẩn điểm 0 thất bại: %s (%d)", do_sensor_error_str(err), err);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Hiệu chuẩn điểm 0 cảm biến DO thành công!");
    return ESP_OK;
}

esp_err_t do_sensor_calibrate_slope(void){
  if(!s_initialized){
    ESP_LOGE(TAG, "Lỗi: Cảm biến DO chưa được khởi tạo!");
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE){
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Bắt đầu gửi lệnh hiệu chuẩn độ dốc (Slope Calibration) đến DO Sensor (Slave: %d)", s_cfg.slave_id);

  int err = modbus_write_reg(s_cfg.slave_id, 0x1004, 0x0000);
  xSemaphoreGive(s_mutex);

  if(err != 0){
        ESP_LOGE(TAG, "Lưu hiệu chuẩn độ dốc thất bại: %s (%d)", do_sensor_error_str(err), err);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Hiệu chuẩn độ dốc cảm biến DO thành công!");
    return ESP_OK;
}

esp_err_t do_sensor_correct_temp(float standard_temp_c){
  if(!s_initialized){
    ESP_LOGE(TAG, "Lỗi: Cảm biến DO chưa được khởi tạo!");
    return ESP_ERR_INVALID_STATE;
  }
  // Chuyển đổi sang giá trị ghi (Nhiệt độ x 10), làm tròn số nguyên gần nhất
  int16_t modbus_val = (int16_t) (standard_temp_c * 10.0f + (standard_temp_c > 0.0f ? 0.5f : -0.5f));

  if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE){
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Bắt đầu gửi lệnh hiệu chỉnh nhiệt độ đến DO Sensor (Slave: %d, value: %d)", s_cfg.slave_id, modbus_val);

  int err = modbus_write_reg(s_cfg.slave_id, 0x1010, (uint16_t)modbus_val);
  xSemaphoreGive(s_mutex);

  if(err != 0){
        ESP_LOGE(TAG, "Lưu hiệu chuẩn nhiệt độ thất bại: %s (%d)", do_sensor_error_str(err), err);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Hiệu chuẩn nhiệt độ cảm biến DO thành công!");
    return ESP_OK;
}

esp_err_t do_sensor_reset(void){
    if(!s_initialized){
    ESP_LOGE(TAG, "Lỗi: Cảm biến DO chưa được khởi tạo!");
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE){
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Bắt đầu gửi lệnh khôi phục cài đặt gốc đến DO Sensor (Slave: %d)", s_cfg.slave_id);

  // Ghi giá trị 0 vào thanh ghi 0x2020
    int err = modbus_write_reg(s_cfg.slave_id, 0x2020, 0x0000);
    xSemaphoreGive(s_mutex);
    if(err != 0){
        ESP_LOGE(TAG, "Khôi phục cài đặt gốc thất bại: %s (%d)", do_sensor_error_str(err), err);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Khôi phục cài đặt gốc cảm biến DO thành công!");
    return ESP_OK;
}
esp_err_t do_sensor_set_salinity(float salinity_psu){
  if (!s_initialized){
    ESP_LOGE(TAG, "Lỗi: Cảm biến DO chưa được khởi tạo");
    return ESP_ERR_INVALID_STATE;
  }

  // Bước 1: Kiểm tra giới hạn độ mặn đầu vào hợp lệ
  if (salinity_psu < 0.0f || salinity_psu > 100.0f){
    ESP_LOGE(TAG, "Lỗi: Giá trị độ mặn %.1f PSU nằm ngoài khoảng cho phép (0 - 100)!", salinity_psu);
    return ESP_ERR_INVALID_ARG;
  }

  // Bước 2: Nhân với 10 làm tròn về số nguyên gần nhất (ép kiểu uint16_t để ghi modbus)
  uint16_t modbus_val = (uint16_t)(salinity_psu * 10.0f + 0.5f);

  // Lock mutex bảo vệ tài nguyên UART
  if(xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE){
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Gửi lệnh bù độ mặn đến DO Sensor (Slave ID: %d, Register: 0x1020, Value: %d)", 
             s_cfg.slave_id, modbus_val);

  // Bước 3: Gọi hàm ghi Modbus ghi single register (Function code 06)
  int err = modbus_write_reg(s_cfg.slave_id, 0x1020, modbus_val);

  xSemaphoreGive(s_mutex);

  if (err != 0){
    ESP_LOGE(TAG, "Bù độ mặn thất bại: %s (%d)", do_sensor_error_str(err), err);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Cấu hình bù độ mặn cảm biến DO thành công!");
  return ESP_OK;
}



