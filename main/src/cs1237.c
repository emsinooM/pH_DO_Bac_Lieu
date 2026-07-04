#include "cs1237.h"
#include "esp_log.h"
#include "esp_rom_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CS1237_DRIVER";

// Spinlock để bảo vệ các đoạn code bit-banging khỏi ngắt của FreeRTOS
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static inline void delay_us(uint32_t us) { esp_rom_delay_us(us); }

void init_system_gpios(void) {
  // Reset only the temperature pins to disconnect from USB Serial JTAG or other
  // functions
  gpio_reset_pin(TEMP_SCLK_PIN);
  gpio_reset_pin(TEMP_DATA_PIN);

  gpio_config_t io_conf = {};

  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1ULL << TEMP_SCLK_PIN);
  io_conf.pull_down_en = 0;
  io_conf.pull_up_en = 0;
  gpio_config(&io_conf);

  gpio_set_level(TEMP_SCLK_PIN, 0);

  io_conf.pin_bit_mask = (1ULL << TEMP_DATA_PIN);
  // io_conf.mode = GPIO_MODE_INPUT_OUTPUT_OD;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pull_up_en = 1;
  io_conf.pull_down_en = 0;
  gpio_config(&io_conf);

  // Thả nổi chân DATA ban đầu ở mức HIGH (High-Z)
  gpio_set_level(TEMP_DATA_PIN, 1);

  // --- Khởi tạo các chân GPIO cho pH ---
  gpio_reset_pin(PH_SCLK_PIN);
  gpio_reset_pin(PH_DATA_PIN);

  // Cấu hình chân Clock của pH là OUTPUT
  io_conf.pin_bit_mask = (1ULL << PH_SCLK_PIN);
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pull_down_en = 0;
  io_conf.pull_up_en = 0;
  gpio_config(&io_conf);
  gpio_set_level(PH_SCLK_PIN, 0);

  // Cấu hình chân Data của pH là INPUT
  io_conf.pin_bit_mask = (1ULL << PH_DATA_PIN);
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pull_up_en = 1;
  io_conf.pull_down_en = 0;
  gpio_config(&io_conf);
  gpio_set_level(PH_DATA_PIN, 1);
}

void write_cs1237_config(gpio_num_t sclk_pin, gpio_num_t data_pin,
uint8_t config_val) {
  // gpio_set_level(data_pin, 1);
  int timeout = 10000;
  while (gpio_get_level(data_pin) == 1) {
    delay_us(50);
    if (--timeout <= 0) {
      ESP_LOGE(TAG, "Timeout chờ DRDY khi cấu hình!");
      return;
    }
  }
  portENTER_CRITICAL(&mux);
  gpio_set_direction(data_pin, GPIO_MODE_OUTPUT);

  // 1. Gửi 27 xung đầu tiên để đọc/bỏ qua dữ liệu cũ
  for (int i = 0; i < 27; i++) {
    gpio_set_level(sclk_pin, 1);
    delay_us(50);
    gpio_set_level(sclk_pin, 0);
    delay_us(50);
  }

  // Gửi 2 xung dummy (xung 28, 29)
  delay_us(50);
  for (int i = 0; i < 2; i++) {
    gpio_set_level(sclk_pin, 1);
    delay_us(50);
    gpio_set_level(sclk_pin, 0);
    delay_us(50);
  }

  // 3. Gửi lệnh Ghi thanh ghi (0x65) qua 8 xung SCLK (xung 30 -> 36)
  uint8_t cmd = CS1237_CMD_WRITE_REG;
  for (int i = 0; i < 7; i++) {
    gpio_set_level(data_pin, (cmd >> (6 - i)) & 0x01);
    delay_us(50);
    gpio_set_level(sclk_pin, 1);
    delay_us(50);
    gpio_set_level(sclk_pin, 0);
    delay_us(50);
  }

  // 4. Xung 37 (1 xung Turnaround): Chân DOUT của CS1237 tiếp tục giữ nguyên là
  // INPUT (đối với chip) MCU giữ nguyên cấu hình OUTPUT và chỉ phát 1 xung nhịp
  gpio_set_level(sclk_pin, 1);
  delay_us(50);
  gpio_set_level(sclk_pin, 0);
  delay_us(50);

  // 5. Gửi giá trị cấu hình 8-bit qua 8 xung tiếp theo (38 -> 45)
  for (int i = 0; i < 8; i++) {
    gpio_set_level(data_pin, (config_val >> (7 - i)) & 0x01);
    delay_us(50);
    gpio_set_level(sclk_pin, 1);
    delay_us(50);
    gpio_set_level(sclk_pin, 0);
    delay_us(50);
  }

  // Gửi xung thứ 46 để kết thúc quá trình ghi
  gpio_set_level(sclk_pin, 1);
  delay_us(50);
  gpio_set_level(sclk_pin, 0);
  delay_us(50);

  // Trả lại chân DATA làm INPUT để tiếp tục đọc dữ liệu thông thường
  gpio_set_direction(data_pin, GPIO_MODE_INPUT);

  portEXIT_CRITICAL(&mux);
}

int32_t read_cs1237_raw(gpio_num_t sclk_pin, gpio_num_t data_pin) {
  uint32_t data = 0;
  int timeout = 10000;

  // Đảm bảo chân DATA đang được thả nổi ở mức HIGH (OD) để CS1237 có thể kéo
  // xuống LOW khi sẵn sàng
  gpio_set_level(data_pin, 1);

  while (gpio_get_level(data_pin) == 1) {
    delay_us(10);
    if (--timeout <= 0)
      return 0;
  }

  portENTER_CRITICAL(&mux);

  for (int i = 23; i >= 0; i--) {
    gpio_set_level(sclk_pin, 1);
    delay_us(10);
    if (gpio_get_level(data_pin)) {
      data |= (1UL << i);
    }
    gpio_set_level(sclk_pin, 0);
    delay_us(10);
  }

  // int update_flag = -1;
  for (int i = 0; i < 3; i++) {
    gpio_set_level(sclk_pin, 1);
    delay_us(10);
    gpio_set_level(sclk_pin, 0);
    delay_us(10);

    // if (i == 0) {
    //     update_flag = gpio_get_level(data_pin);
    // }
  }

  portEXIT_CRITICAL(&mux);

  // if (update_flag != -1) {
  //     ESP_LOGI("CS1237_DEBUG", "Cờ Update (Xung 25): %d", update_flag);
  // }

  int32_t raw_signed;
  if (data & 0x00800000) {
    raw_signed = (int32_t)(data | 0xFF000000);
  } else {
    raw_signed = (int32_t)data;
  }

  return raw_signed;
}

uint8_t read_cs1237_config(gpio_num_t sclk_pin, gpio_num_t data_pin) {
  uint8_t config_read = 0;
  int timeout = 10000;

  // Chờ chip sẵn sàng
  gpio_set_level(data_pin, 1);
  while (gpio_get_level(data_pin) == 1) {
    delay_us(50);
    if (--timeout <= 0)
      return 0xFF; // Lỗi timeout
  }

  portENTER_CRITICAL(&mux);

  // 1. Xung 1-27: Đọc bỏ qua dữ liệu ADC cũ
  for (int i = 0; i < 27; i++) {
    gpio_set_level(sclk_pin, 1);
    delay_us(40);
    gpio_set_level(sclk_pin, 0);
    delay_us(40);
  }

  // 2. Xung 28-29 (Turnaround)
  delay_us(10);
  for (int i = 0; i < 2; i++) {
    gpio_set_level(sclk_pin, 1);
    delay_us(40);
    gpio_set_level(sclk_pin, 0);
    delay_us(40);
  }

  // 3. Xung 30-36: Gửi lệnh ĐỌC thanh ghi (0x56)
  // Cấu hình chân DATA thành OUTPUT để truyền lệnh ghi cho CS1237
  gpio_set_direction(data_pin, GPIO_MODE_OUTPUT);

  uint8_t cmd = 0x56; // Mã lệnh Đọc
  for (int i = 0; i < 7; i++) {
    gpio_set_level(data_pin, (cmd >> (6 - i)) & 0x01);
    delay_us(40);
    gpio_set_level(sclk_pin, 1);
    delay_us(40);
    gpio_set_level(sclk_pin, 0);
    delay_us(40);
  }

  // 4. Xung 37 (Turnaround Đọc):
  // MCU PHẢI chuyển chân DATA lại thành INPUT để nhận dữ liệu do CS1237 phản hồi
  gpio_set_direction(data_pin, GPIO_MODE_INPUT);

  gpio_set_level(sclk_pin, 1);
  delay_us(40);
  gpio_set_level(sclk_pin, 0);
  delay_us(40);

  // 5. Xung 38-45: Đọc 8-bit dữ liệu cấu hình từ chip (MSB xuất ra trước)
  for (int i = 0; i < 8; i++) {
    gpio_set_level(sclk_pin, 1);
    delay_us(40);
    gpio_set_level(sclk_pin, 0);
    delay_us(40);
    // Đọc mức logic do CS1237 đẩy ra
    if (gpio_get_level(data_pin)) {
      config_read |= (1 << (7 - i));
    }
  }

  // 6. Xung 46: Kết thúc
  gpio_set_level(sclk_pin, 1);
  delay_us(40);
  gpio_set_level(sclk_pin, 0);
  delay_us(40);

  portEXIT_CRITICAL(&mux);

  return config_read;
}
