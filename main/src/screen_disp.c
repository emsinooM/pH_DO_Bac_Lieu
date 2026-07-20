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

  esp_rom_delay_us(20); // Delay ngắn để ổn định chân DC/CS trước khi truyền

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
static const uint8_t s_degree_font5x7[5] = {0x00, 0x03, 0x03, 0x00, 0x00};

static const uint8_t *get_font5x7_data(char c) {
  uint8_t uc = (uint8_t)c;
  if (uc == 0xB0 || uc == 0xDF || uc == 176 || uc == 223) {
    return s_degree_font5x7;
  }
  if (uc < 0x20 || uc > 0x7E) {
    uc = '?';
  }
  return font5x7[uc - 0x20];
}

void LCD_DrawChar(uint8_t x, uint8_t y, char c, uint8_t color) {
  const uint8_t *col_data = get_font5x7_data(c);
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
    if ((uint8_t)*str == 0xC2) {
      str++;
      continue;
    }
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
  const uint8_t *col_data = get_font5x7_data(c);
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
    if ((uint8_t)*str == 0xC2) {
      str++;
      continue;
    }
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

__attribute__((unused)) void LCD_DrawInt(uint8_t x, uint8_t y, int32_t val, uint8_t color) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%ld", (long)val);
  LCD_DrawString(x, y, buf, color);
}

void LCD_DrawCharClipped(int16_t x, int16_t y, char c, uint8_t min_x, uint8_t max_x, uint8_t color) {
  const uint8_t *col_data = get_font5x7_data(c);
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
  uint16_t char_idx = 0;
  for (uint16_t i = 0; i < len; i++) {
    if ((uint8_t)str[i] == 0xC2) continue;
    int16_t char_x = x + char_idx * 6 - scroll_x;
    if (char_x + 6 > x && char_x < max_x) {
      LCD_DrawCharClipped(char_x, y, str[i], x, max_x, color);
    }
    char_idx++;
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

__attribute__((unused)) void LCD_DrawLargeString(uint8_t x, uint8_t y, const char *str, uint8_t color) {
  uint8_t cx = x;
  while (*str) {
    uint8_t adv = LCD_DrawLargeChar(cx, y, *str, color);
    if (adv == 0)
      adv = 8;     /* ký tự không hỗ trợ: skip 8px */
    cx += adv + 1; /* 1px khoảng cách giữa ký tự */
    str++;
  }
}

__attribute__((unused)) uint8_t LCD_GetLargeStringWidth(const char *str) {
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

uint8_t LCD_DrawMediumChar(uint8_t x, uint8_t y, char c, uint8_t color) {
  int8_t idx = large_font_idx(c);
  if (idx < 0)
    return 0;

  const medium_font_char_t *fc = &g_medium_font[idx];
  uint8_t w = fc->width;

  for (uint8_t row = 0; row < 17; row++) {
    uint16_t line = fc->bitmap[row];
    for (uint8_t col = 0; col < w; col++) {
      uint8_t pix = (line >> (w - 1 - col)) & 1;
      LCD_DrawPixel(x + col, y + row, pix ? color : (color ^ 1));
    }
  }
  return w;
}

void LCD_DrawMediumString(uint8_t x, uint8_t y, const char *str, uint8_t color) {
  uint8_t cx = x;
  while (*str) {
    uint8_t adv = LCD_DrawMediumChar(cx, y, *str, color);
    if (adv == 0)
      adv = 6;     /* ký tự không hỗ trợ: skip 6px */
    cx += adv + 1; /* 1px khoảng cách giữa ký tự */
    str++;
  }
}

uint8_t LCD_GetMediumStringWidth(const char *str) {
  uint8_t total = 0;
  while (*str) {
    int8_t idx = large_font_idx(*str);
    if (idx >= 0)
      total += g_medium_font[idx].width + 1;
    else
      total += 7;
    str++;
  }
  if (total > 0)
    total--;
  return total;
}

static uint8_t lcd_text_width(const char *str) {
  return (uint8_t)(strlen(str) * 6);
}

static uint8_t lcd_center_in_panel(uint8_t panel_x, uint8_t panel_w,
                                   uint8_t content_w) {
  if (content_w >= panel_w) {
    return panel_x;
  }
  return panel_x + (panel_w - content_w) / 2;
}

static uint8_t LCD_DrawChar2x(uint8_t x, uint8_t y, char c, uint8_t color) {
  if (c < 0x20 || c > 0x7E)
    return 0;
  const uint8_t *col_data = font5x7[c - 0x20];
  for (uint8_t col = 0; col < 5; col++) {
    uint8_t b = col_data[col];
    for (uint8_t row = 0; row < 7; row++) {
      if ((b >> row) & 1) {
        uint8_t px = x + col * 2;
        uint8_t py = y + row * 2;
        LCD_DrawPixel(px, py, color);
        LCD_DrawPixel(px + 1, py, color);
        LCD_DrawPixel(px, py + 1, color);
        LCD_DrawPixel(px + 1, py + 1, color);
      }
    }
  }
  return 10;
}

static void LCD_DrawString2x(uint8_t x, uint8_t y, const char *str, uint8_t color) {
  uint8_t cx = x;
  while (*str) {
    uint8_t adv = LCD_DrawChar2x(cx, y, *str, color);
    cx += adv + 2;
    str++;
  }
}

static uint8_t LCD_GetStringWidth2x(const char *str) {
  uint8_t len = strlen(str);
  if (len == 0) return 0;
  return (uint8_t)(len * 12 - 2);
}

static uint8_t LCD_DrawChar3x(uint8_t x, uint8_t y, char c, uint8_t color) {
  if (c < 0x20 || c > 0x7E)
    return 0;
  const uint8_t *col_data = font5x7[c - 0x20];
  for (uint8_t col = 0; col < 5; col++) {
    uint8_t b = col_data[col];
    for (uint8_t row = 0; row < 7; row++) {
      if ((b >> row) & 1) {
        uint8_t px = x + col * 3;
        uint8_t py = y + row * 3;
        for (uint8_t dx = 0; dx < 3; dx++) {
          for (uint8_t dy = 0; dy < 3; dy++) {
            LCD_DrawPixel(px + dx, py + dy, color);
          }
        }
      }
    }
  }
  return 15;
}

static void LCD_DrawString3x(uint8_t x, uint8_t y, const char *str, uint8_t color) {
  uint8_t cx = x;
  while (*str) {
    uint8_t adv = LCD_DrawChar3x(cx, y, *str, color);
    cx += adv + 3;
    str++;
  }
}

static uint8_t LCD_GetStringWidth3x(const char *str) {
  uint8_t len = strlen(str);
  if (len == 0) return 0;
  return (uint8_t)(len * 18 - 3);
}

static void lcd_draw_dual_metric(uint8_t panel_x, uint8_t panel_w, uint8_t val_y,
                                 uint8_t unit_y, const char *val_str,
                                 const char *unit_str, bool unit_medium) {
  uint8_t val_w = LCD_GetStringWidth2x(val_str);
  uint8_t unit_w =
      unit_medium ? LCD_GetMediumStringWidth(unit_str) : lcd_text_width(unit_str);

  uint8_t val_x = lcd_center_in_panel(panel_x, panel_w, val_w);
  uint8_t unit_x = lcd_center_in_panel(panel_x, panel_w, unit_w);

  LCD_DrawString2x(val_x, val_y, val_str, LCD_COLOR_ON);
  if (unit_medium) {
    LCD_DrawMediumString(unit_x, unit_y, unit_str, LCD_COLOR_ON);
  } else {
    LCD_DrawString(unit_x, unit_y, unit_str, LCD_COLOR_ON);
  }
}

/* =====================================================================
 * Bieu do thoi gian thuc (rolling chart + truc X/Y)
 * ===================================================================== */
/* So diem = cua so thoi gian / CHART_SEC_PER_POINT.
 * 24 diem x 2s = ~46s (hien thi tam 45 giay). */
#define CHART_PLOT_FULL_W  24
#define CHART_PLOT_HALF_W  24
#define CHART_PLOT_H       26
#define CHART_AREA_TOP     12
#define CHART_Y_LBL_W      14
#define CHART_X_LBL_H      7
#define CHART_PH_AXIS_MIN   0.0f
#define CHART_PH_AXIS_MAX   14.0f
#define CHART_PH_FOCUS_LO   6.0f
#define CHART_PH_FOCUS_HI   9.0f
#define CHART_PH_INIT       7.0f
/* Ty le chieu cao: 0-6 | 6-9 (lon) | 9-14 */
#define CHART_PH_FRAC_LOW   0.18f
#define CHART_PH_FRAC_MID   0.55f
#define CHART_PH_FRAC_HIGH  0.27f
#define CHART_DO_MIN       0.0f
#define CHART_DO_MAX       20.0f
/* Moi diem du lieu cach nhau bao nhieu giay (tick==0 moi 2s trong lcd_demo_task) */
#define CHART_SEC_PER_POINT 2

static float s_chart_ph_full[CHART_PLOT_FULL_W];
static float s_chart_do_full[CHART_PLOT_FULL_W];
static float s_chart_ph_half[CHART_PLOT_HALF_W];
static float s_chart_do_half[CHART_PLOT_HALF_W];
static uint8_t s_chart_ph_full_cnt = 0;
static uint8_t s_chart_do_full_cnt = 0;
static uint8_t s_chart_ph_half_cnt = 0;
static uint8_t s_chart_do_half_cnt = 0;
static display_view_t s_chart_last_view = DISP_VIEW_COUNT;
static display_mode_t s_chart_last_mode = DISP_MODE_COUNT;

static void chart_reset_buffers(void) {
  for (uint8_t i = 0; i < CHART_PLOT_FULL_W; i++) {
    s_chart_ph_full[i] = CHART_PH_INIT;
    s_chart_do_full[i] = CHART_DO_MIN;
  }
  for (uint8_t i = 0; i < CHART_PLOT_HALF_W; i++) {
    s_chart_ph_half[i] = CHART_PH_INIT;
    s_chart_do_half[i] = CHART_DO_MIN;
  }
  s_chart_ph_full_cnt = 0;
  s_chart_do_full_cnt = 0;
  s_chart_ph_half_cnt = 0;
  s_chart_do_half_cnt = 0;
}

static void chart_sync_mode(void) {
  if (g_display_view != DISP_VIEW_CHART) {
    return;
  }
  if (s_chart_last_view != g_display_view ||
      s_chart_last_mode != g_display_mode) {
    chart_reset_buffers();
    s_chart_last_view = g_display_view;
    s_chart_last_mode = g_display_mode;
  }
}

static void chart_push_ltr(float *buf, uint8_t len, uint8_t *fill_cnt, float val) {
  if (*fill_cnt < len) {
    buf[*fill_cnt] = val;
    (*fill_cnt)++;
  } else {
    memmove(buf, buf + 1, (len - 1) * sizeof(float));
    buf[len - 1] = val;
    *fill_cnt = len;
  }
}

/* Vi tri X: ve TU TRAI SANG PHAI theo buoc co dinh (window diem).
 *  - Diem cu nhat (idx = 0) o mep trai, diem moi hon dan sang phai.
 *  - Khi chua day: ben phai de trong, "but" chay dan sang phai.
 *  - Khi day (n == window): diem moi nhat cham mep phai, sau do troi.
 */
static uint8_t chart_x_ltr(uint8_t px, uint8_t pw, uint8_t idx, uint8_t window) {
  if (window <= 1) {
    return px;
  }
  uint16_t off = (uint16_t)idx * (uint16_t)(pw - 1) / (uint16_t)(window - 1);
  if (off > (uint16_t)(pw - 1)) {
    off = (uint16_t)(pw - 1);
  }
  return (uint8_t)(px + off);
}

static void chart_draw_dot(uint8_t cx, uint8_t cy) {
  /* Cham nho 2x2 danh dau tung diem du lieu */
  LCD_DrawPixel(cx, cy, LCD_COLOR_ON);
  LCD_DrawPixel(cx + 1, cy, LCD_COLOR_ON);
  LCD_DrawPixel(cx, cy + 1, LCD_COLOR_ON);
  LCD_DrawPixel(cx + 1, cy + 1, LCD_COLOR_ON);
}

static void chart_draw_head_marker(uint8_t cx, uint8_t cy) {
  if (cx > 0) {
    cx--;
  }
  if (cy > 0) {
    cy--;
  }
  LCD_FillRect(cx, cy, 3, 3, LCD_COLOR_ON);
}



/* Nhan truc X (ve tu trai sang phai, cua so thoi gian co dinh):
 *  - "Dau but" (diem moi nhat = bay gio) DI CHUYEN tu trai sang phai;
 *    moc thoi gian bay gio chay theo dau but.
 *  - Moc ben trai la diem cu nhat dang hien tren bieu do.
 *  - Co gio thuc (RTC/NTP) -> hien gio dong ho HH:MM; chua co -> tuoi (-m:ss..0).
 */
static void chart_draw_x_labels_ltr(uint8_t px, uint8_t pw, uint8_t axis_y,
                                    uint8_t lbl_y, uint8_t window,
                                    uint8_t point_cnt, bool compact) {
  (void)lbl_y;
  (void)window;
  (void)point_cnt;
  uint8_t divs = compact ? 2 : 4;   /* so vach chia nho tren truc X */

  /* Vach chia nho tren truc X (luon ve de thay thang chia) */
  for (uint8_t d = 0; d <= divs; d++) {
    uint8_t x = (uint8_t)(px + (uint16_t)d * (pw - 1) / divs);
    LCD_DrawVLine(x, (uint8_t)(axis_y - 1), 3, LCD_COLOR_ON);
  }
}

static uint8_t chart_value_to_y(float val, float ymin, float ymax, uint8_t plot_y,
                                uint8_t plot_h) {
  if (val < ymin) {
    val = ymin;
  }
  if (val > ymax) {
    val = ymax;
  }
  float span = ymax - ymin;
  if (span <= 0.0f) {
    return plot_y;
  }
  float norm = (val - ymin) / span;
  return (uint8_t)(plot_y + (plot_h - 1) * (1.0f - norm));
}

static void chart_draw_axis_number(uint8_t x, uint8_t y, float val) {
  char buf[6];
  int whole = (int)(val + 0.5f);
  if (val == (float)((int)val) && whole >= 0 && whole <= 99) {
    snprintf(buf, sizeof(buf), "%d", whole);
  } else {
    snprintf(buf, sizeof(buf), "%.1f", val);
  }
  LCD_DrawString(x, y, buf, LCD_COLOR_ON);
}

static float chart_ph_to_norm(float ph) {
  if (ph <= CHART_PH_FOCUS_LO) {
    return (ph / CHART_PH_FOCUS_LO) * CHART_PH_FRAC_LOW;
  }
  if (ph <= CHART_PH_FOCUS_HI) {
    float t = (ph - CHART_PH_FOCUS_LO) / (CHART_PH_FOCUS_HI - CHART_PH_FOCUS_LO);
    return CHART_PH_FRAC_LOW + t * CHART_PH_FRAC_MID;
  }
  float t = (ph - CHART_PH_FOCUS_HI) / (CHART_PH_AXIS_MAX - CHART_PH_FOCUS_HI);
  if (t > 1.0f) {
    t = 1.0f;
  }
  return CHART_PH_FRAC_LOW + CHART_PH_FRAC_MID + t * CHART_PH_FRAC_HIGH;
}

static uint8_t chart_ph_value_to_y(float ph, uint8_t plot_y, uint8_t plot_h) {
  if (ph < CHART_PH_AXIS_MIN) {
    ph = CHART_PH_AXIS_MIN;
  }
  if (ph > CHART_PH_AXIS_MAX) {
    ph = CHART_PH_AXIS_MAX;
  }
  float norm = chart_ph_to_norm(ph);
  return (uint8_t)(plot_y + (plot_h - 1) * (1.0f - norm));
}

static void chart_draw_ph_axis_labels(uint8_t area_x, uint8_t py, uint8_t ph,
                                      bool compact) {
  chart_draw_axis_number(area_x, py, CHART_PH_AXIS_MAX);
  chart_draw_axis_number(area_x, (uint8_t)(py + ph - 7), CHART_PH_AXIS_MIN);
  if (!compact) {
    uint8_t y7 = chart_ph_value_to_y(7.0f, py, ph);
    if (y7 > py + 2 && y7 < py + ph - 10) {
      chart_draw_axis_number(area_x, (uint8_t)(y7 - 3), 7.0f);
    }
  }
}

static void lcd_draw_chart_panel(uint8_t area_x, uint8_t area_y, uint8_t area_w,
                                 uint8_t area_h, const float *buf, uint8_t buf_len,
                                 uint8_t fill_cnt, float ymin, float ymax,
                                 const char *unit, bool compact, bool ph_piecewise) {
  if (area_w < 20 || area_h < 14 || buf_len < 2) {
    return;
  }

  const uint8_t px = (uint8_t)(area_x + CHART_Y_LBL_W);
  const uint8_t py = area_y;
  const uint8_t pw = (uint8_t)(area_w - CHART_Y_LBL_W - 1);
  const uint8_t ph = (uint8_t)(area_h - CHART_X_LBL_H);
  const uint8_t axis_y = (uint8_t)(py + ph);
  const uint8_t lbl_y = (uint8_t)(area_y + area_h - 6);
  const float ymid = (ymin + ymax) * 0.5f;

  if (ph_piecewise) {
    chart_draw_ph_axis_labels(area_x, py, ph, compact);
  } else {
    chart_draw_axis_number(area_x, py, ymax);
    if (!compact) {
      chart_draw_axis_number(area_x, (uint8_t)(py + ph / 2 - 3), ymid);
    }
    chart_draw_axis_number(area_x, (uint8_t)(py + ph - 7), ymin);
  }

  if (unit != NULL && !compact) {
    LCD_DrawString(area_x, (uint8_t)(py + 8), unit, LCD_COLOR_ON);
  } else if (unit != NULL && compact) {
    uint8_t uw = lcd_text_width(unit);
    LCD_DrawString((uint8_t)(area_x + area_w - uw - 2), py, unit, LCD_COLOR_ON);
  }

  LCD_DrawVLine(px, py, ph + 1, LCD_COLOR_ON);
  LCD_DrawHLine(px, axis_y, pw, LCD_COLOR_ON);

  chart_draw_x_labels_ltr(px, pw, axis_y, lbl_y, buf_len, fill_cnt, compact);

  uint8_t n = fill_cnt;
  if (n > buf_len) {
    n = buf_len;
  }

  for (uint8_t i = 1; i < n; i++) {
    uint8_t x0 = chart_x_ltr(px, pw, (uint8_t)(i - 1), buf_len);
    uint8_t x1 = chart_x_ltr(px, pw, i, buf_len);
    uint8_t y0;
    uint8_t y1;
    if (ph_piecewise) {
      y0 = chart_ph_value_to_y(buf[i - 1], py, ph);
      y1 = chart_ph_value_to_y(buf[i], py, ph);
    } else {
      y0 = chart_value_to_y(buf[i - 1], ymin, ymax, py, ph);
      y1 = chart_value_to_y(buf[i], ymin, ymax, py, ph);
    }
    LCD_DrawLine(x0, y0, x1, y1, LCD_COLOR_ON);
  }

  /* Cham danh dau tung diem du lieu */
  for (uint8_t i = 0; i < n; i++) {
    uint8_t dx = chart_x_ltr(px, pw, i, buf_len);
    uint8_t dy = ph_piecewise ? chart_ph_value_to_y(buf[i], py, ph)
                              : chart_value_to_y(buf[i], ymin, ymax, py, ph);
    chart_draw_dot(dx, dy);
  }

  if (n > 0) {
    uint8_t hx = chart_x_ltr(px, pw, (uint8_t)(n - 1), buf_len);
    uint8_t hy = ph_piecewise ? chart_ph_value_to_y(buf[n - 1], py, ph)
                              : chart_value_to_y(buf[n - 1], ymin, ymax, py, ph);
    chart_draw_head_marker(hx, hy);
  }
}

static void lcd_draw_measurement_chart(float ph_val, float do_val, bool do_valid) {
  /* Bieu do toan man hinh: tu y=CHART_AREA_TOP toi day man hinh (y=63) */
  const uint8_t area_h = (uint8_t)(63 - CHART_AREA_TOP);

  if (g_display_mode == DISP_MODE_PH) {
    lcd_draw_chart_panel(4, CHART_AREA_TOP, 120, area_h, s_chart_ph_full,
                         CHART_PLOT_FULL_W, s_chart_ph_full_cnt, CHART_PH_AXIS_MIN,
                         CHART_PH_AXIS_MAX, NULL, false, true);
  } else if (g_display_mode == DISP_MODE_DO) {
    lcd_draw_chart_panel(4, CHART_AREA_TOP, 120, area_h, s_chart_do_full,
                         CHART_PLOT_FULL_W, s_chart_do_full_cnt, CHART_DO_MIN,
                         CHART_DO_MAX, NULL, false, false);
  } else {
    const uint8_t split_x = 64;
    LCD_DrawVLine(split_x - 1, CHART_AREA_TOP, area_h + 1, LCD_COLOR_ON);
    LCD_DrawVLine(split_x, CHART_AREA_TOP, area_h + 1, LCD_COLOR_ON);

    lcd_draw_chart_panel(2, CHART_AREA_TOP, 60, area_h, s_chart_ph_half,
                         CHART_PLOT_HALF_W, s_chart_ph_half_cnt, CHART_PH_AXIS_MIN,
                         CHART_PH_AXIS_MAX, "pH", true, true);
    if (do_valid) {
      lcd_draw_chart_panel(66, CHART_AREA_TOP, 60, area_h, s_chart_do_half,
                           CHART_PLOT_HALF_W, s_chart_do_half_cnt, CHART_DO_MIN,
                           CHART_DO_MAX, "DO", true, false);
    } else {
      LCD_DrawRect(66, CHART_AREA_TOP, 60, area_h, LCD_COLOR_ON);
      if (g_sys_lang == LANG_VI) {
        LCD_DrawString(74, CHART_AREA_TOP + 12, "DO N/A", LCD_COLOR_ON);
      } else {
        LCD_DrawString(74, CHART_AREA_TOP + 12, "DO N/A", LCD_COLOR_ON);
      }
    }
  }
}

static void lcd_draw_measurement_numbers(const char *ph_str, const char *do_str) {
  if (g_display_mode == DISP_MODE_PH) {
    uint8_t ph_w = LCD_GetStringWidth3x(ph_str);
    uint8_t unit_w = lcd_text_width("pH");
    uint8_t block_w = ph_w + 5 + unit_w;
    uint8_t ph_x = (LCD_WIDTH - block_w) / 2;
    LCD_DrawString3x(ph_x, 17, ph_str, LCD_COLOR_ON);
    LCD_DrawString(ph_x + ph_w + 5, 30, "pH", LCD_COLOR_ON);
  } else if (g_display_mode == DISP_MODE_DO) {
    uint8_t do_w = LCD_GetStringWidth3x(do_str);
    uint8_t unit_w = lcd_text_width("mg/L");
    uint8_t block_w = do_w + 5 + unit_w;
    uint8_t do_x = (LCD_WIDTH - block_w) / 2;
    LCD_DrawString3x(do_x, 17, do_str, LCD_COLOR_ON);
    LCD_DrawString(do_x + do_w + 5, 30, "mg/L", LCD_COLOR_ON);
  } else {
    const uint8_t split_x = 64;
    const uint8_t left_x = 2;
    const uint8_t right_x = 65;
    const uint8_t panel_w = 61;
    const uint8_t area_y = 11;
    const uint8_t area_bottom = 45;
    const uint8_t val_y = 14;
    const uint8_t unit_y = 34;

    /* Clear panel display area to eliminate leftover pixel artifacts */
    LCD_FillRect(left_x, area_y, panel_w, area_bottom - area_y, LCD_COLOR_OFF);
    LCD_FillRect(right_x, area_y, panel_w, area_bottom - area_y, LCD_COLOR_OFF);

    LCD_DrawVLine(split_x - 1, area_y, area_bottom - area_y + 1, LCD_COLOR_ON);
    LCD_DrawVLine(split_x, area_y, area_bottom - area_y + 1, LCD_COLOR_ON);
    LCD_DrawHLine(left_x, area_bottom, panel_w, LCD_COLOR_ON);
    LCD_DrawHLine(right_x, area_bottom, panel_w, LCD_COLOR_ON);

    lcd_draw_dual_metric(left_x, panel_w, val_y, unit_y, ph_str, "pH", false);
    lcd_draw_dual_metric(right_x, panel_w, val_y, unit_y, do_str, "mg/L", false);
  }
}

static void lcd_draw_top_bar_value(const char *ph_str, const char *do_str) {
  char header[24];
  if (g_display_mode == DISP_MODE_PH) {
    snprintf(header, sizeof(header), "pH %s", ph_str);
  } else if (g_display_mode == DISP_MODE_DO) {
    snprintf(header, sizeof(header), "DO %s mg/L", do_str);
  } else {
    snprintf(header, sizeof(header), "p:%s d:%s", ph_str, do_str);
  }
  LCD_DrawString(4, 1, header, LCD_COLOR_OFF);
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

  /* ── Hiển thị màn hình khởi động (Boot screen) ── */
  LCD_Clear();
  // Vẽ logo màn hình khởi động tùy chỉnh (128x64) tràn viền
  LCD_DrawBitmap(0, 0, 128, 64, bitmap_custom);
  LCD_Flush();

#if 0 /* GIẢ LẬP: Treo ở màn hình khởi động để test webserver (Sửa 1 -> 0 khi muốn chạy bình thường) */
  ESP_LOGW(TAG, "GIA LAP: Dang treo o man hinh khoi dong de test Webserver...");
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
#else
  vTaskDelay(pdMS_TO_TICKS(5000));
#endif

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
  char temp_val_num_str[10] = "25.3";
  char unit_char = 'C';
  char time_str[32] = "--:-- --";
  char date_str[32] = "2026-05-22";

  while (1) {
    /* ── 1. Xử lý nút bấm ── */
    menu_handle_buttons();

    /* ── 2. Nếu đang nhập PIN hoặc menu → render và lặp lại ── */
    if (g_menu.in_pin_entry || g_menu.in_menu) {
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
      bool ph_valid = status.ph_valid;
      do_val = status.do_mg_l;
      do_valid = status.do_valid;

      bool temp_valid = false;
      // Chế độ pH dùng nhiệt độ pH. Chế độ DO và song song dùng nhiệt độ DO
      // (nếu lỗi dùng pH làm fallback).
      if (g_display_mode == DISP_MODE_PH) {
        temp_val = status.temperature;
        temp_valid = status.temp_valid;
      } else {
        temp_val = status.do_valid ? status.do_temp_c : status.temperature;
        temp_valid = status.do_valid ? (status.do_temp_c >= 0.0f && status.do_temp_c <= 60.0f) : status.temp_valid;
      }

      /* Format chuỗi */
      if (ph_valid) {
        snprintf(ph_str, sizeof(ph_str), "%.2f", ph_val);
      } else {
        snprintf(ph_str, sizeof(ph_str), "N/A");
      }

      if (do_valid) {
        snprintf(do_str, sizeof(do_str), "%.2f", do_val);
      } else {
        snprintf(do_str, sizeof(do_str), "N/A");
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

      bool is_f = (g_temp_mode == TEMP_MODE_ATC_F || g_temp_mode == TEMP_MODE_MTC_F);
      if (!temp_valid) {
        snprintf(temp_val_num_str, sizeof(temp_val_num_str), "N/A");
      } else if (is_f) {
        float temp_val_f = temp_val * 1.8f + 32.0f;
        snprintf(temp_val_num_str, sizeof(temp_val_num_str), "%.1f", temp_val_f);
      } else {
        snprintf(temp_val_num_str, sizeof(temp_val_num_str), "%.1f", temp_val);
      }
      unit_char = is_f ? 'F' : 'C';

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

    if (g_display_view == DISP_VIEW_CHART) {
      chart_sync_mode();
      if (tick == 0) {
        if (g_display_mode == DISP_MODE_PH) {
          chart_push_ltr(s_chart_ph_full, CHART_PLOT_FULL_W, &s_chart_ph_full_cnt,
                         ph_val);
        } else if (g_display_mode == DISP_MODE_DO && do_valid) {
          chart_push_ltr(s_chart_do_full, CHART_PLOT_FULL_W, &s_chart_do_full_cnt,
                         do_val);
        } else if (g_display_mode == DISP_MODE_DUAL) {
          chart_push_ltr(s_chart_ph_half, CHART_PLOT_HALF_W, &s_chart_ph_half_cnt,
                         ph_val);
          if (do_valid) {
            chart_push_ltr(s_chart_do_half, CHART_PLOT_HALF_W, &s_chart_do_half_cnt,
                           do_val);
          }
        }
        g_lcd_need_redraw = true;
      }
    } else {
      s_chart_last_view = g_display_view;
      s_chart_last_mode = g_display_mode;
    }
    tick = (tick + 1) % 20;

    if (g_lcd_need_redraw) {
      /* Vẽ màn hình đo lường
       * ─────────────────────────────────────────────────────────────
       *  y= 0..11 : Top bar    (nền đen, chữ trắng)
       *  y=12..45 : Giá trị pH lớn / DO lớn / Song song
       *  y=46..63 : Bottom bar (nền đen, chữ trắng)
       * ─────────────────────────────────────────────────────────────
       */
      LCD_Clear();

      if (g_display_view == DISP_VIEW_CHART) {
        /* ── Che do bieu do TOAN MAN HINH ──
         *  y=0..9  : Top bar (gia tri hien tai + nhiet do)
         *  y=12..63: Bieu do phu gan het man hinh
         */
        LCD_FillRect(0, 0, 128, 10, LCD_COLOR_ON);
        lcd_draw_top_bar_value(ph_str, do_str);
        /* Nhiệt độ góc phải top bar trong chế độ đồ thị (có biểu tượng °) */
        {
          uint8_t num_w = (uint8_t)(strlen(temp_val_num_str) * 6);
          uint8_t total_w = num_w + 12;
          uint8_t tx = (total_w + 8 < 128) ? (uint8_t)(128 - total_w - 4) : 90;

          LCD_DrawString(tx, 1, temp_val_num_str, LCD_COLOR_OFF);

          uint8_t degree_x = tx + num_w + 1;
          LCD_DrawPixel(degree_x, 1, LCD_COLOR_OFF);
          LCD_DrawPixel(degree_x + 1, 1, LCD_COLOR_OFF);
          LCD_DrawPixel(degree_x, 2, LCD_COLOR_OFF);
          LCD_DrawPixel(degree_x + 1, 2, LCD_COLOR_OFF);

          char unit_buf[2] = { unit_char, '\0' };
          LCD_DrawString(tx + num_w + 6, 1, unit_buf, LCD_COLOR_OFF);
        }
        lcd_draw_measurement_chart(ph_val, do_val, do_valid);
      } else {
        /* ── Che do so ── */
        /* Top bar */
        LCD_FillRect(0, 0, 128, 10, LCD_COLOR_ON);
        if (g_sys_lang == LANG_VI) {
          LCD_DrawString(4, 1, "MebiEco", LCD_COLOR_OFF);
        } else {
          LCD_DrawString(4, 1, "MebiEco", LCD_COLOR_OFF);
        }
        LCD_DrawString(80, 1, "RS485", LCD_COLOR_OFF);

        /* Đường viền dọc vùng giữa (2px mỗi bên) */
        LCD_DrawVLine(0, 10, 36, LCD_COLOR_ON);
        LCD_DrawVLine(1, 10, 36, LCD_COLOR_ON);
        LCD_DrawVLine(126, 10, 36, LCD_COLOR_ON);
        LCD_DrawVLine(127, 10, 36, LCD_COLOR_ON);

        lcd_draw_measurement_numbers(ph_str, do_str);

        /* Bottom bar (Nền đen, chữ trắng) */
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
        { // DISP_MODE_DO hoặc DISP_MODE_DUAL
          LCD_DrawString(4, 47, do_sat_str, LCD_COLOR_OFF);
        }
        /* Hiển thị số nhiệt độ căn chuẩn khớp với chuỗi giờ bên dưới */
        if (strcmp(temp_val_num_str, "N/A") == 0) {
          LCD_DrawString(86, 47, "N/A", LCD_COLOR_OFF);
          LCD_DrawPixel(106, 47, LCD_COLOR_OFF);
          LCD_DrawPixel(107, 47, LCD_COLOR_OFF);
          LCD_DrawPixel(106, 48, LCD_COLOR_OFF);
          LCD_DrawPixel(107, 48, LCD_COLOR_OFF);
          char unit_str_buf[2] = { unit_char, '\0' };
          LCD_DrawString(111, 47, unit_str_buf, LCD_COLOR_OFF);
        } else {
          char int_part[10] = {0};
          char frac_char[2] = {0, 0};
          char *dot_ptr = strchr(temp_val_num_str, '.');
          if (dot_ptr) {
            int len_int = dot_ptr - temp_val_num_str;
            strncpy(int_part, temp_val_num_str, len_int);
            int_part[len_int] = '\0';
            frac_char[0] = dot_ptr[1];
          } else {
            strcpy(int_part, temp_val_num_str);
            frac_char[0] = '0';
          }

          uint8_t int_w = (uint8_t)(strlen(int_part) * 6);
          uint8_t int_x = 94 - int_w;

          /* Phần nguyên (giãn về bên trái) */
          LCD_DrawString(int_x, 47, int_part, LCD_COLOR_OFF);
          /* Dấu chấm (.) */
          LCD_DrawString(94, 47, ".", LCD_COLOR_OFF);
          /* Số thập phân sau dấu chấm (khớp thẳng đứng với số phút thứ 2 tại x = 100) */
          LCD_DrawString(100, 47, frac_char, LCD_COLOR_OFF);

          /* Ký hiệu độ (2x2 px) tại x = 106 */
          LCD_DrawPixel(106, 47, LCD_COLOR_OFF);
          LCD_DrawPixel(107, 47, LCD_COLOR_OFF);
          LCD_DrawPixel(106, 48, LCD_COLOR_OFF);
          LCD_DrawPixel(107, 48, LCD_COLOR_OFF);

          /* Chữ đơn vị C/F tại x = 111 */
          char unit_str_buf[2] = { unit_char, '\0' };
          LCD_DrawString(111, 47, unit_str_buf, LCD_COLOR_OFF);
        }
        LCD_DrawString(4, 56, date_str, LCD_COLOR_OFF);
        LCD_DrawString(76, 56, time_str, LCD_COLOR_OFF);
      }

      LCD_Flush();
      g_lcd_need_redraw = false;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

__attribute__((unused)) void LCD_Start_Task(void) {
  xTaskCreatePinnedToCore(lcd_demo_task, "LCD_Task", 4096, NULL, 5, NULL, 1);
}

__attribute__((unused)) void screen_update_values(float ph, float temp) {
  s_real_ph = ph;
  s_real_temp = temp;
  g_lcd_need_redraw =
      true; // Báo hiệu để task vẽ lại màn hình ngay khi có dữ liệu mới
}

__attribute__((unused)) void LCD_GetFramebuffer(uint8_t *dest) {
  if (dest != NULL) {
    memcpy(dest, fb, sizeof(fb));
  }
}
