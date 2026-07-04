/**
 * @file    Module_man_hinh_cam_bien.c
 * @brief   Driver màn hình đồ họa GMG12864-06D (ST7565) – ESP32 WROVER-E
 *
 *  Kết nối chân:
 *   CS   → GPIO 13   SCL → GPIO 12 (SPI CLK)
 *   RST  → Mạch ngoài SDA → GPIO 11 (SPI MOSI)
 *   A0   → GPIO 10 (DC)
 */

#include "screen_disp.h"
#include "ph_temp.h"
#include "screen_menu.h"
#include "large_font.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static const char *TAG = "GMG12864";

/* =====================================================================
 * Framebuffer RAM (128 × 64 / 8 = 1024 bytes)
 * ===================================================================== */
static uint8_t fb[LCD_PAGES][LCD_WIDTH]
    __attribute__((aligned(4))); /* fb[page][col] */

static spi_device_handle_t s_spi;

uint8_t g_lcd_contrast = 30;
uint8_t g_lcd_resistor_ratio = 0;
volatile bool g_lcd_need_redraw = true;
static volatile float s_real_ph = 7.00f;
static volatile float s_real_temp = 25.0f;

/* =====================================================================
 * Giao tiếp SPI nội bộ
 * ===================================================================== */
static void lcd_send(const uint8_t *data, int len, bool is_data) {
  gpio_set_level(LCD_PIN_DC, is_data ? 1 : 0);
  gpio_set_level(LCD_PIN_CS, 0); // Kéo CS xuống LOW để chọn thiết bị

  esp_rom_delay_us(10); // Delay ngắn để ổn định chân DC/CS trước khi truyền

  spi_transaction_t t = {0};
  if (len <= 4) {
    t.flags = SPI_TRANS_USE_TXDATA; // Truyền trực tiếp dữ liệu từ struct (SPI FIFO), bỏ qua DMA
    t.length = len * 8;
    memcpy(t.tx_data, data, len);
  } else {
    t.length = len * 8;
    t.tx_buffer = data; // Truyền qua DMA (chỉ áp dụng cho static aligned buffer lớn)
  }

  ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
  gpio_set_level(LCD_PIN_CS, 1); // Kéo CS lên HIGH sau khi truyền xong
}

static inline void lcd_cmd(uint8_t cmd) { lcd_send(&cmd, 1, false); }

static inline void lcd_dat(uint8_t dat) { lcd_send(&dat, 1, true); }

void LCD_Clear_DDRAM(void) {
  static uint8_t zero_buf[132] __attribute__((aligned(4)));
  memset(zero_buf, 0x00, sizeof(zero_buf));
  for (uint8_t page = 0; page < LCD_PAGES; page++) {
    /* Gộp 3 lệnh chọn Page và Cột thành 1 mảng để gửi đồng thời */
    uint8_t cmds[3] = {
        0xB0 | page, /* Set page address */
        0x10,        /* Set column address high nibble to 0 */
        0x00         /* Set column address low nibble to 0 */
    };
    lcd_send(cmds, 3, false);      // Gửi lệnh định vị
    lcd_send(zero_buf, 132, true); // Gửi dữ liệu xóa
  }
}

/* =====================================================================
 * Khởi tạo
 * ===================================================================== */
lcd_err_t LCD_Init(void) {
  ESP_LOGI(TAG, "Khoi tao SPI + GMG12864-06D (ST7565)...");

  /* --- Cấu hình GPIO DC và CS --- */
  gpio_config_t io_cfg = {
      .pin_bit_mask =
          (1ULL << LCD_PIN_DC) | (1ULL << LCD_PIN_CS) | (1ULL << LCD_PIN_RST),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_cfg);
  gpio_set_level(LCD_PIN_CS, 1); // CS mặc định ở mức cao (idle HIGH)
  gpio_set_level(LCD_PIN_RST, 0);
  /* --- Khởi tạo SPI bus (HSPI) --- */
  spi_bus_config_t buscfg = {
      .miso_io_num = -1,
      .mosi_io_num = LCD_PIN_MOSI,
      .sclk_io_num = LCD_PIN_CLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = LCD_WIDTH * LCD_PAGES + 8,
  };
  esp_err_t ret = spi_bus_initialize(
      LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO); // Kích hoạt lại DMA cho các gói
                                               // tin lớn (như Flush màn hình)
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_initialize that bai: %s", esp_err_to_name(ret));
    return LCD_ERR;
  }

  /* --- Thêm thiết bị vào bus --- */
  spi_device_interface_config_t devcfg = {
      .clock_speed_hz = LCD_SPI_FREQ_HZ,
      .mode = 3,
      .spics_io_num = -1, // Điều khiển CS bằng phần mềm (Software CS)
      .queue_size = 7,
  };
  ret = spi_bus_add_device(LCD_SPI_HOST, &devcfg, &s_spi);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_add_device that bai: %s", esp_err_to_name(ret));
    return LCD_ERR;
  }

  /* --- Reset phần cứng (đã đấu nối cứng bên ngoài mạch) --- */
  // vTaskDelay(pdMS_TO_TICKS(50));

  /* --- Chuỗi lệnh khởi tạo ST7565R (Đồng bộ U8g2 NHD-C12864) --- */

  vTaskDelay(pdMS_TO_TICKS(50));
  gpio_set_level(LCD_PIN_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(50));

  // lcd_cmd(0xE2); /* Software Reset */
  // vTaskDelay(pdMS_TO_TICKS(10));

  lcd_cmd(0xAE); /* Display OFF */
  lcd_cmd(0x40); /* Set Display Start Line = 0 */

  lcd_cmd(0xA1); /* ADC set to reverse (SEG direction) */
  lcd_cmd(0xC0); /* COM Output scan direction normal (0xC8 if reversed) */

  lcd_cmd(0xA6); /* Normal Display */
  lcd_cmd(0xA2); /* LCD Bias: 1/9 bias */

  /* Bật toàn bộ các mạch nguồn (Booster + Regulator + Follower) */
  lcd_cmd(0x2F);
  vTaskDelay(pdMS_TO_TICKS(50));

  /* Thiết lập tỷ số nhân áp (Booster Ratio Select) */
  lcd_cmd(0xF8); /* Booster Ratio Command */
  lcd_cmd(0x01); /* 4x Booster */

  /* Thiết lập điện áp tỷ số điện trở nội */
  lcd_cmd(0x25); /* Resistor Ratio (v0 voltage resistor ratio, 0x20 - 0x27) */

  /* Thiết lập tương phản (Contrast) */
  lcd_cmd(0x81); /* Electronic Volume (Contrast command) */
  lcd_cmd(30);   /* Sử dụng biến tương phản toàn cục */

  lcd_cmd(0xA4); /* Gửi lệnh hiển thị bình thường từ bộ đệm RAM */
  lcd_cmd(0xAF); /* Display ON */

  /* --- Xóa màn hình --- */
  LCD_Clear();
  LCD_Flush();

  ESP_LOGI(TAG, "ST7565R san sang!");
  return LCD_OK;
}

/* =====================================================================
 * Điều khiển hiển thị
 * ===================================================================== */
void LCD_DisplayOn(void) { lcd_cmd(0xAF); }
void LCD_DisplayOff(void) { lcd_cmd(0xAE); }

void LCD_SetContrast(uint8_t val) {
  if (val > 0x3F)
    val = 0x3F;
  lcd_cmd(0x81);
  lcd_cmd(val);
}

void LCD_SetResistorRatio(uint8_t val) {
  if (val > 7)
    val = 7;
  lcd_cmd(0x20 | val);
}

/* =====================================================================
 * Framebuffer
 * ===================================================================== */
void LCD_Clear(void) { memset(fb, 0x00, sizeof(fb)); }

void LCD_Flush(void) {
  /*
   * ST7565R có 132 cột DDRAM nhưng màn hình chỉ hiển thị 128 cột.
   * LCD_COLUMN_OFFSET = 4 → pixel hiển thị bắt đầu từ cột DDRAM thứ 4.
   * Để triệt để loại bỏ rác DDRAM (sọc dọc), ta ghi đè toàn bộ 132 cột:
   *   - Cột 0..3   : padding 0x00 (vùng ẩn trước offset)
   *   - Cột 4..131 : dữ liệu framebuffer (128 byte)
   *
   * Buffer phải static + aligned(4) để đảm bảo tương thích GDMA trên ESP32-S3.
   */
  static uint8_t row_buf[132] __attribute__((aligned(4)));

  for (uint8_t page = 0; page < LCD_PAGES; page++) {
    /* Gộp 3 lệnh chọn Page và Cột thành 1 mảng để gửi đồng thời */
    uint8_t cmds[3] = {
        0xB0 | page, /* Set page address */
        0x10,        /* Set column address high nibble to 0 */
        0x00         /* Set column address low nibble to 0 */
    };
    /* Gửi 3 lệnh định vị trong 1 transaction */
    lcd_send(cmds, 3, false);
    /* Điền zero cho vùng offset đầu */
    memset(row_buf, 0x00, LCD_COLUMN_OFFSET);
    /* Copy framebuffer vào sau vùng offset */
    memcpy(row_buf + LCD_COLUMN_OFFSET, fb[page], LCD_WIDTH);
    /* Gửi toàn bộ 132 byte trong 1 transaction SPI */
    lcd_send(row_buf, 132, true);
  }
}

/* =====================================================================
 * Vẽ pixel
 * ===================================================================== */
void LCD_DrawPixel(uint8_t x, uint8_t y, uint8_t color) {
  if (x >= LCD_WIDTH || y >= LCD_HEIGHT)
    return;
  if (color) {
    fb[y >> 3][x] |= (1 << (y & 7));
  } else {
    fb[y >> 3][x] &= ~(1 << (y & 7));
  }
}

/* =====================================================================
 * Đường thẳng
 * ===================================================================== */
void LCD_DrawHLine(uint8_t x, uint8_t y, uint8_t w, uint8_t color) {
  for (uint8_t i = 0; i < w && (x + i) < LCD_WIDTH; i++)
    LCD_DrawPixel(x + i, y, color);
}

void LCD_DrawVLine(uint8_t x, uint8_t y, uint8_t h, uint8_t color) {
  for (uint8_t i = 0; i < h && (y + i) < LCD_HEIGHT; i++)
    LCD_DrawPixel(x, y + i, color);
}

void LCD_DrawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1,
                  uint8_t color) {
  int dx = abs((int)x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs((int)y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (1) {
    LCD_DrawPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1)
      break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

/* =====================================================================
 * Hình chữ nhật
 * ===================================================================== */
void LCD_DrawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color) {
  LCD_DrawHLine(x, y, w, color);
  LCD_DrawHLine(x, y + h - 1, w, color);
  LCD_DrawVLine(x, y, h, color);
  LCD_DrawVLine(x + w - 1, y, h, color);
}

void LCD_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color) {
  for (uint8_t row = 0; row < h && (y + row) < LCD_HEIGHT; row++)
    LCD_DrawHLine(x, y + row, w, color);
}

/* =====================================================================
 * Hình tròn (Midpoint Circle)
 * ===================================================================== */
void LCD_DrawCircle(uint8_t cx, uint8_t cy, uint8_t r, uint8_t color) {
  int x = 0, y = r, d = 1 - r;
  while (x <= y) {
    LCD_DrawPixel(cx + x, cy + y, color);
    LCD_DrawPixel(cx - x, cy + y, color);
    LCD_DrawPixel(cx + x, cy - y, color);
    LCD_DrawPixel(cx - x, cy - y, color);
    LCD_DrawPixel(cx + y, cy + x, color);
    LCD_DrawPixel(cx - y, cy + x, color);
    LCD_DrawPixel(cx + y, cy - x, color);
    LCD_DrawPixel(cx - y, cy - x, color);
    if (d < 0)
      d += 2 * x + 3;
    else {
      d += 2 * (x - y) + 5;
      y--;
    }
    x++;
  }
}

/* =====================================================================
 * Ký tự / chuỗi (font 5×7)
 * ===================================================================== */
void LCD_DrawChar(uint8_t x, uint8_t y, char c, uint8_t color) {
  if (c < 0x20 || c > 0x7E)
    c = '?';
  const uint8_t *col_data = font5x7[c - 0x20];
  for (uint8_t col = 0; col < 5; col++) {
    uint8_t line = col_data[col];
    for (uint8_t row = 0; row < 8; row++) {
      if (line & (1 << row))
        LCD_DrawPixel(x + col, y + row, color);
      else
        LCD_DrawPixel(x + col, y + row, color ^ 1);
    }
  }
  /* Cột trống giữa các ký tự */
  for (uint8_t row = 0; row < 8; row++)
    LCD_DrawPixel(x + 5, y + row, color ^ 1);
}

void LCD_DrawString(uint8_t x, uint8_t y, const char *str, uint8_t color) {
  uint8_t cx = x;
  while (*str) {
    if (*str == '\n') {
      cx = x;
      y += 8;
    } else {
      if (cx + 6 > LCD_WIDTH) {
        cx = x;
        y += 8;
      }
      LCD_DrawChar(cx, y, *str, color);
      cx += 6;
    }
    str++;
  }
}

void LCD_DrawCharScaled(uint8_t x, uint8_t y, char c, uint8_t scale_x,
                        uint8_t scale_y, uint8_t color) {
  if (c < 0x20 || c > 0x7E)
    c = '?';
  const uint8_t *col_data = font5x7[c - 0x20];
  for (uint8_t col = 0; col < 5; col++) {
    uint8_t line = col_data[col];
    for (uint8_t row = 0; row < 8; row++) {
      uint8_t px_color = (line & (1 << row)) ? color : (color ^ 1);
      for (uint8_t sx = 0; sx < scale_x; sx++) {
        for (uint8_t sy = 0; sy < scale_y; sy++) {
          LCD_DrawPixel(x + col * scale_x + sx, y + row * scale_y + sy,
                        px_color);
        }
      }
    }
  }
  /* Cột trống giữa các ký tự */
  for (uint8_t row = 0; row < 8; row++) {
    for (uint8_t sx = 0; sx < scale_x; sx++) {
      for (uint8_t sy = 0; sy < scale_y; sy++) {
        LCD_DrawPixel(x + 5 * scale_x + sx, y + row * scale_y + sy, color ^ 1);
      }
    }
  }
}

void LCD_DrawStringScaled(uint8_t x, uint8_t y, const char *str,
                          uint8_t scale_x, uint8_t scale_y, uint8_t color) {
  uint8_t cx = x;
  while (*str) {
    if (*str == '\n') {
      cx = x;
      y += 8 * scale_y;
    } else {
      if (cx + 6 * scale_x > LCD_WIDTH) {
        cx = x;
        y += 8 * scale_y;
      }
      LCD_DrawCharScaled(cx, y, *str, scale_x, scale_y, color);
      cx += 6 * scale_x;
    }
    str++;
  }
}

void LCD_DrawInt(uint8_t x, uint8_t y, int32_t val, uint8_t color) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%ld", (long)val);
  LCD_DrawString(x, y, buf, color);
}

void LCD_DrawCharClipped(int16_t x, int16_t y, char c, uint8_t min_x, uint8_t max_x, uint8_t color) {
  if (c < 0x20 || c > 0x7E)
    c = '?';
  const uint8_t *col_data = font5x7[c - 0x20];
  for (uint8_t col = 0; col < 5; col++) {
    int16_t px_x = x + col;
    if (px_x >= min_x && px_x < max_x) {
      uint8_t line = col_data[col];
      for (uint8_t row = 0; row < 8; row++) {
        if (y + row < 64) {
          if (line & (1 << row))
            LCD_DrawPixel(px_x, y + row, color);
          else
            LCD_DrawPixel(px_x, y + row, color ^ 1);
        }
      }
    }
  }
  int16_t px_x = x + 5;
  if (px_x >= min_x && px_x < max_x) {
    for (uint8_t row = 0; row < 8; row++) {
      if (y + row < 64) {
        LCD_DrawPixel(px_x, y + row, color ^ 1);
      }
    }
  }
}

void LCD_DrawStringScroll(uint8_t x, uint8_t y, const char *str, uint8_t max_x, int16_t scroll_x, uint8_t color) {
  uint16_t len = strlen(str);
  for (uint16_t i = 0; i < len; i++) {
    int16_t char_x = x + i * 6 - scroll_x;
    if (char_x + 6 > x && char_x < max_x) {
      LCD_DrawCharClipped(char_x, y, str[i], x, max_x, color);
    }
  }
}

/* =====================================================================
 * Phông chữ lớn (16×28 pixel, Arial Bold)
 * ===================================================================== */

static int8_t large_font_idx(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c == '.')
    return 10;
  if (c == 'p')
    return 11;
  if (c == 'H')
    return 12;
  if (c == 'C')
    return 13;
  if (c == 'F')
    return 14;
  if (c == '-')
    return 15;
  return -1;
}

uint8_t LCD_DrawLargeChar(uint8_t x, uint8_t y, char c, uint8_t color) {
  int8_t idx = large_font_idx(c);
  if (idx < 0)
    return 0;

  const font_char_t *fc = &g_large_font[idx];
  uint8_t w = fc->width;

  for (uint8_t row = 0; row < 28; row++) {
    uint16_t line = fc->bitmap[row];
    for (uint8_t col = 0; col < 16; col++) {
      uint8_t pix = (line >> (15 - col)) & 1;
      LCD_DrawPixel(x + col, y + row, pix ? color : (color ^ 1));
    }
  }
  return w;
}

void LCD_DrawLargeString(uint8_t x, uint8_t y, const char *str, uint8_t color) {
  uint8_t cx = x;
  while (*str) {
    uint8_t adv = LCD_DrawLargeChar(cx, y, *str, color);
    if (adv == 0)
      adv = 8;     /* ký tự không hỗ trợ: skip 8px */
    cx += adv + 1; /* 1px khoảng cách giữa ký tự */
    str++;
  }
}

uint8_t LCD_GetLargeStringWidth(const char *str) {
  uint8_t total = 0;
  while (*str) {
    int8_t idx = large_font_idx(*str);
    if (idx >= 0)
      total += g_large_font[idx].width + 1;
    else
      total += 9; /* ký tự không hỗ trợ */
    str++;
  }
  if (total > 0)
    total--; /* bỏ khoảng cách cuối */
  return total;
}

/* =====================================================================
 * Bitmap 1-bit (MSB trái, row-major)
 * ===================================================================== */
void LCD_DrawBitmap(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                    const uint8_t *bmp) {
  uint16_t byte_idx = 0;
  for (uint8_t row = 0; row < h; row++) {
    for (uint8_t col = 0; col < w; col++) {
      uint8_t bit_idx = col % 8;
      if (bit_idx == 0 && col != 0)
        byte_idx++;
      uint8_t pix = (bmp[byte_idx] >> (7 - bit_idx)) & 1;
      LCD_DrawPixel(x + col, y + row, pix ? LCD_COLOR_ON : LCD_COLOR_OFF);
    }
    byte_idx++;
  }
}

/* =====================================================================
 * FreeRTOS Task demo với chế độ tự chẩn đoán (Diagnostic)
 * ===================================================================== */
static void lcd_demo_task(void *arg) {
  ESP_LOGI(TAG, "VE GIAO DIEN APURE A10 + MENU");

  /* ── Khởi tạo nút bấm ── */
  menu_init();

  /* ── Hiển thị màn hình khởi động (Boot screen) trong 5 giây ── */
  LCD_Clear();
  // Vẽ logo màn hình khởi động tùy chỉnh (128x64) tràn viền
  LCD_DrawBitmap(0, 0, 128, 64, bitmap_custom);
  LCD_Flush();
  vTaskDelay(pdMS_TO_TICKS(5000));

  /* Tải cấu hình lưu từ NVS */
  menu_load_settings();

  /* ── Biến trạng thái màn hình đo lường ── */
  float ph_val = 7.00f;
  float do_val = 0.00f;
  float temp_val = 25.3f;
  bool do_valid = false;
  int tick = 0; /* 0..19  → 20×50 ms = 1 giây    */

  char ph_str[10] = "7.00";
  char do_str[12] = "0.00";
  char do_sat_str[16] = "Sat: --.-%";
  char temp_str[12] = "25.3 C";
  char time_str[32] = "--:-- --";
  char date_str[32] = "2026-05-22";

  while (1) {
    /* ── 1. Xử lý nút bấm ── */
    menu_handle_buttons();

    /* ── 2. Nếu đang ở menu → render menu và lặp lại ── */
    if (g_menu.in_menu) {
      menu_render();
      vTaskDelay(pdMS_TO_TICKS(50));
      tick = 0;                 /* reset tick khi vừa thoát menu */
      g_lcd_need_redraw = true; /* Vẽ lại màn hình đo lường khi thoát menu */
      continue;
    }

    /* ── 3. Màn hình đo lường ── */

    /* Cập nhật giá trị mỗi 1 giây (tick=0) */
    if (tick == 0) {
      PH_Temp_Sensor_Status_t status = Get_Sensor_Status();
      ph_val = status.ph;
      do_val = status.do_mg_l;
      do_valid = status.do_valid;

      // Chế độ pH dùng nhiệt độ pH. Chế độ DO và song song dùng nhiệt độ DO
      // (nếu lỗi dùng pH làm fallback).
      if (g_display_mode == DISP_MODE_PH) {
        temp_val = status.temperature;
      } else {
        temp_val = status.do_valid ? status.do_temp_c : status.temperature;
      }

      /* Format chuỗi */
      snprintf(ph_str, sizeof(ph_str), "%.2f", ph_val);
      if (do_valid) {
        snprintf(do_str, sizeof(do_str), "%.2f", do_val);
      } else {
        snprintf(do_str, sizeof(do_str), "---");
      }
      if (g_sys_lang == LANG_VI) {
        if (do_valid) {
          snprintf(do_sat_str, sizeof(do_sat_str), "Oxy: %.1f%%", status.do_saturation_pct);
        } else {
          snprintf(do_sat_str, sizeof(do_sat_str), "Oxy: N/A");
        }
      } else {
        if (do_valid) {
          snprintf(do_sat_str, sizeof(do_sat_str), "Sat: %.1f%%", status.do_saturation_pct);
        } else {
          snprintf(do_sat_str, sizeof(do_sat_str), "Sat: N/A");
        }
      }
    //   snprintf(temp_str, sizeof(temp_str), "%.1f C", temp_val);
    bool is_f = (g_temp_mode == TEMP_MODE_ATC_F || g_temp_mode == TEMP_MODE_MTC_F);
        if (is_f) {
            float temp_val_f = temp_val * 1.8f + 32.0f;
            snprintf(temp_str, sizeof(temp_str), "%.1f F", temp_val_f);
        } else {
            snprintf(temp_str, sizeof(temp_str), "%.1f C", temp_val);
        }

      // Đọc thời gian thực tế từ hệ thống
      time_t now;
      struct tm timeInfo;
      time(&now);
      localtime_r(&now, &timeInfo);

      // Nếu thời gian đã được đồng bộ (năm lớn hơn hoặc bằng 2000, tương ứng
      // tm_year >= 100)
      if (timeInfo.tm_year >= 100) {
        strftime(time_str, sizeof(time_str), "%I:%M %p", &timeInfo);
        if (g_date_format == DATE_FORMAT_DD_MM_YYYY) {
          strftime(date_str, sizeof(date_str), "%d-%m-%Y", &timeInfo);
        } else if (g_date_format == DATE_FORMAT_MM_DD_YYYY) {
          strftime(date_str, sizeof(date_str), "%m-%d-%Y", &timeInfo);
        } else {
          strftime(date_str, sizeof(date_str), "%Y-%m-%d", &timeInfo);
        }
      } else {
        snprintf(time_str, sizeof(time_str), "--:-- --");
        if (g_date_format == DATE_FORMAT_DD_MM_YYYY) {
          snprintf(date_str, sizeof(date_str), "22-05-2026");
        } else if (g_date_format == DATE_FORMAT_MM_DD_YYYY) {
          snprintf(date_str, sizeof(date_str), "05-22-2026");
        } else {
          snprintf(date_str, sizeof(date_str), "2026-05-22");
        }
      }

      static uint8_t log_cnt = 0;
      if (++log_cnt >=
          5) { // tick == 0 xảy ra mỗi 2 giây, do đó 5 lần là 10 giây
        log_cnt = 0;
        ESP_LOGI(TAG, "Thoi gian hien thi tren LCD: %s %s", date_str, time_str);
      }

      g_lcd_need_redraw = true;
    }
    tick = (tick + 1) % 20;

    if (g_lcd_need_redraw) {
      /* Vẽ màn hình đo lường
       * ─────────────────────────────────────────────────────────────
       *  y= 0..11 : Top bar    (nền đen, chữ trắng)
       *  y=12..43 : Giá trị pH lớn / DO lớn / Song song
       *  y=44..63 : Bottom bar (nền đen, chữ trắng)
       * ─────────────────────────────────────────────────────────────
       */
      LCD_Clear();

      /* Top bar */
      LCD_FillRect(0, 0, 128, 10, LCD_COLOR_ON);
      if (g_sys_lang == LANG_VI) {
        LCD_DrawString(4, 1, "Dang Do...", LCD_COLOR_OFF);
      } else {
        LCD_DrawString(4, 1, "Measuring", LCD_COLOR_OFF);
      }

      if (g_display_mode == DISP_MODE_DUAL) {
        // Trong chế độ song song, vẽ nhiệt độ ở góc trên bên phải của top bar thay cho RS485
        LCD_DrawString(88, 1, temp_str, LCD_COLOR_OFF);
        int unit_idx = 0;
        while (temp_str[unit_idx] != '\0' && temp_str[unit_idx] != 'C' &&
               temp_str[unit_idx] != 'F') {
          unit_idx++;
        }
        int degree_x = 88 + unit_idx * 6 - 5;
        LCD_DrawPixel(degree_x, 1, LCD_COLOR_OFF);
        LCD_DrawPixel(degree_x + 1, 1, LCD_COLOR_OFF);
        LCD_DrawPixel(degree_x, 2, LCD_COLOR_OFF);
        LCD_DrawPixel(degree_x + 1, 2, LCD_COLOR_OFF);
      } else {
        LCD_DrawString(80, 1, "RS485", LCD_COLOR_OFF);
      }

      /* Đường viền dọc vùng giữa (2px mỗi bên) */
      LCD_DrawVLine(0, 10, 36, LCD_COLOR_ON);
      LCD_DrawVLine(1, 10, 36, LCD_COLOR_ON);
      LCD_DrawVLine(126, 10, 36, LCD_COLOR_ON);
      LCD_DrawVLine(127, 10, 36, LCD_COLOR_ON);

      if (g_display_mode == DISP_MODE_PH) {
        /* Giá trị pH lớn sử dụng Arial Bold */
        uint8_t ph_width = LCD_GetLargeStringWidth(ph_str);
        uint8_t ph_x = (LCD_WIDTH - ph_width - 14) / 2; // Căn giữa, trừ 14px cho "pH"
        LCD_DrawLargeString(ph_x, 14, ph_str, LCD_COLOR_ON);
        LCD_DrawString(ph_x + ph_width + 3, 33, "pH", LCD_COLOR_ON);
      } else if (g_display_mode == DISP_MODE_DO) {
        /* Giá trị DO lớn sử dụng Arial Bold (bao gồm cả "---" khi không hợp lệ) */
        uint8_t do_width = LCD_GetLargeStringWidth(do_str);
        uint8_t do_x = (LCD_WIDTH - do_width - 25) / 2; // Căn giữa, trừ 25px cho "mg/L"
        LCD_DrawLargeString(do_x, 14, do_str, LCD_COLOR_ON);
        LCD_DrawString(do_x + do_width + 3, 33, "mg/L", LCD_COLOR_ON);
      } else { // DISP_MODE_DUAL
        /* Hiển thị song song pH và DO đều dùng Arial Bold */
        // 1. Dòng 1: pH (y=11)
        uint8_t ph_width = LCD_GetLargeStringWidth(ph_str);
        uint8_t ph_x = (128 - ph_width - 15) / 2;
        LCD_DrawLargeString(ph_x, 11, ph_str, LCD_COLOR_ON);
        LCD_DrawString(ph_x + ph_width + 3, 30, "pH", LCD_COLOR_ON);

        // 2. Dòng 2: DO (y=36)
        uint8_t do_width = LCD_GetLargeStringWidth(do_str);
        uint8_t do_x = (128 - do_width - 25) / 2;
        LCD_DrawLargeString(do_x, 36, do_str, LCD_COLOR_ON);
        LCD_DrawString(do_x + do_width + 3, 55, "mg/L", LCD_COLOR_ON);
      }

      if (g_display_mode != DISP_MODE_DUAL) {
        /* Bottom bar (Chỉ vẽ cho chế độ đo đơn để không bị đè chữ) */
        LCD_FillRect(0, 46, 128, 18, LCD_COLOR_ON);
        if (g_display_mode == DISP_MODE_PH)
        {
          if (g_sys_lang == LANG_VI)
          {
            LCD_DrawString(4, 47, "Dien Cuc pH", LCD_COLOR_OFF);
          }
          else
          {
            LCD_DrawString(4, 47, "pH Electrode", LCD_COLOR_OFF);
          }
        }
        else
        { // DISP_MODE_DO
          LCD_DrawString(4, 47, do_sat_str, LCD_COLOR_OFF);
        }
        /* Ký hiệu độ (2×2 px) đặt động trước ký tự đơn vị (C hoặc F) */
        LCD_DrawString(85, 47, temp_str, LCD_COLOR_OFF);
        int unit_idx = 0;
        while (temp_str[unit_idx] != '\0' && temp_str[unit_idx] != 'C' &&
               temp_str[unit_idx] != 'F') {
          unit_idx++;
        }
        int degree_x = 85 + unit_idx * 6 - 5;
        LCD_DrawPixel(degree_x, 47, LCD_COLOR_OFF);
        LCD_DrawPixel(degree_x + 1, 47, LCD_COLOR_OFF);
        LCD_DrawPixel(degree_x, 48, LCD_COLOR_OFF);
        LCD_DrawPixel(degree_x + 1, 48, LCD_COLOR_OFF);
        LCD_DrawString(4, 56, date_str, LCD_COLOR_OFF);
        LCD_DrawString(76, 56, time_str, LCD_COLOR_OFF);
      }

      LCD_Flush();
      g_lcd_need_redraw = false;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void LCD_Start_Task(void) {
  xTaskCreatePinnedToCore(lcd_demo_task, "LCD_Task", 4096, NULL, 5, NULL, 1);
}

void screen_update_values(float ph, float temp) {
  s_real_ph = ph;
  s_real_temp = temp;
  g_lcd_need_redraw =
      true; // Báo hiệu để task vẽ lại màn hình ngay khi có dữ liệu mới
}

void LCD_GetFramebuffer(uint8_t *dest) {
  if (dest != NULL) {
    memcpy(dest, fb, sizeof(fb));
  }
}
