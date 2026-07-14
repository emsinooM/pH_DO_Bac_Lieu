/**
 * @file    Module_man_hinh_cam_bien.h
 * @brief   Driver màn hình đồ họa GMG12864-06D (Controller ST7565)
 *          dành cho ESP32 WROVER-E
 *
 * @details Giao tiếp SPI phần cứng (HSPI – SPI2_HOST)
 *
 *  Kết nối chân:
 *  ┌─────────┬──────────┬──────────────────────────────────────────┐
 *  │  Module │  ESP32   │  Chức năng                               │
 *  ├─────────┼──────────┼──────────────────────────────────────────┤
 *  │  CS     │  GPIO 13 │  Chip Select (tích cực mức thấp)         │
 *  │  RST/RES│  Mạch    │  Reset cứng bên ngoài                    │
 *  │  A0/DC  │  GPIO 10 │  Register Select  0 = Lệnh | 1 = Dữ liệu│
 *  │  SCL    │  GPIO 12 │  SPI Clock                               │
 *  │  SDA    │  GPIO 11 │  SPI MOSI                                │
 *  └─────────┴──────────┴──────────────────────────────────────────┘
 *
 *  Màn hình : 128 × 64 pixel, đơn sắc (1 bit/pixel)
 *  Controller: ST7565 (tương thích SED1565 / KS0107B / KS0108B)
 */

#ifndef MODULE_MAN_HINH_CAM_BIEN_H
#define MODULE_MAN_HINH_CAM_BIEN_H

#include <stdint.h>
#include <stdbool.h>
#include <Font.h>

/* =====================================================================
 * Kích thước vật lý màn hình
 * ===================================================================== */
#define LCD_WIDTH       128     /**< Chiều rộng (pixel)                    */
#define LCD_HEIGHT      64      /**< Chiều cao  (pixel)                    */
#define LCD_PAGES       8       /**< Số trang = HEIGHT / 8                 */
#define LCD_COLUMN_OFFSET 4     /**< Bù cột hiển thị cho ST7565R           */

/* =====================================================================
 * Định nghĩa GPIO kết nối (khớp với bảng phần cứng)
 * ===================================================================== */
#define LCD_PIN_CS      12      /**< CS   – Chip Select                    */
#define LCD_PIN_DC      9      /**< A0/DC – Register Select (DC)          */
#define LCD_PIN_CLK     11      /**< SCL  – SPI Clock                      */
#define LCD_PIN_MOSI    10      /**< SDA  – SPI MOSI                       */
#define LCD_PIN_RST     4       /**< RES  – Reset phần cứng                */

/* =====================================================================
 * Cấu hình SPI
 * ===================================================================== */
#define LCD_SPI_HOST        SPI2_HOST           /**< SPI2_HOST (FSPI) bus mặc định của ESP32-S3 */
#define LCD_SPI_FREQ_HZ     (1 * 1000 * 1000)   /**< Tốc độ: 1 MHz để truyền nhanh và ổn định */
#define LCD_SPI_MODE        0                   /**< CPOL=0, CPHA=0 (Mode 0) cho ST7565R */

/* =====================================================================
 * Cấu hình nút nhấn
 * ===================================================================== */
#define ESC_BUTTON      13
#define DOWN_BUTTON     14
#define UP_BUTTON       21
#define RIGHT_BUTTON    47
#define ENTER_BUTTON    48

/* =====================================================================
 * Màu vẽ (framebuffer 1-bit)
 * ===================================================================== */
#define LCD_COLOR_OFF   0   /**< Pixel tắt (nền trắng)                     */
#define LCD_COLOR_ON    1   /**< Pixel sáng (điểm đen)                     */

/* =====================================================================
 * Mã lỗi
 * ===================================================================== */
typedef enum {
    LCD_OK  =  0,
    LCD_ERR = -1,
} lcd_err_t;

/* =====================================================================
 * API – Khởi tạo
 * ===================================================================== */

/**
 * @brief  Khởi tạo bus SPI và toàn bộ chuỗi lệnh init cho ST7565
 * @return LCD_OK nếu thành công, LCD_ERR nếu lỗi SPI
 */
lcd_err_t LCD_Init(void);

/* =====================================================================
 * API – Điều khiển hiển thị
 * ===================================================================== */

/** @brief Bật hiển thị (Display ON)  */
void LCD_DisplayOn(void);

/** @brief Tắt hiển thị (Display OFF) */
void LCD_DisplayOff(void);

/**
 * @brief  Điều chỉnh độ tương phản (Electronic Volume)
 * @param  val  Giá trị 0x00 – 0x3F  (mặc định khuyến nghị: 0x20)
 */
void LCD_SetContrast(uint8_t val);
void LCD_SetResistorRatio(uint8_t val);
extern uint8_t g_lcd_resistor_ratio;

/* =====================================================================
 * API – Framebuffer
 * ===================================================================== */

/**
 * @brief  Xóa toàn bộ framebuffer (điền 0x00)
 * @note   Cần gọi LCD_Flush() sau để cập nhật lên màn hình
 */
void LCD_Clear(void);

/**
 * @brief  Ghi toàn bộ framebuffer RAM lên màn hình qua SPI
 * @note   Gọi sau mỗi lần vẽ xong để làm mới hiển thị
 */
void LCD_Flush(void);

/* =====================================================================
 * API – Vẽ cơ bản
 * ===================================================================== */

/**
 * @brief  Đặt 1 pixel trên framebuffer
 * @param  x      Cột  (0 – 127)
 * @param  y      Hàng (0 – 63)
 * @param  color  LCD_COLOR_ON hoặc LCD_COLOR_OFF
 */
void LCD_DrawPixel(uint8_t x, uint8_t y, uint8_t color);

/**
 * @brief  Vẽ đường nằm ngang
 * @param  x      Cột bắt đầu
 * @param  y      Hàng
 * @param  w      Chiều dài (pixel)
 * @param  color  Màu
 */
void LCD_DrawHLine(uint8_t x, uint8_t y, uint8_t w, uint8_t color);

/**
 * @brief  Vẽ đường thẳng đứng
 * @param  x      Cột
 * @param  y      Hàng bắt đầu
 * @param  h      Chiều cao (pixel)
 * @param  color  Màu
 */
void LCD_DrawVLine(uint8_t x, uint8_t y, uint8_t h, uint8_t color);

/**
 * @brief  Vẽ đường thẳng giữa hai điểm (Bresenham)
 * @param  x0, y0  Điểm đầu
 * @param  x1, y1  Điểm cuối
 * @param  color   Màu
 */
void LCD_DrawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t color);

/**
 * @brief  Vẽ hình chữ nhật (chỉ viền)
 * @param  x, y   Góc trên-trái
 * @param  w, h   Chiều rộng, chiều cao
 * @param  color  Màu viền
 */
void LCD_DrawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color);

/**
 * @brief  Vẽ hình chữ nhật đặc
 * @param  x, y   Góc trên-trái
 * @param  w, h   Chiều rộng, chiều cao
 * @param  color  Màu fill
 */
void LCD_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color);

/**
 * @brief  Vẽ hình tròn (chỉ viền) – thuật toán Midpoint Circle
 * @param  cx, cy  Tâm hình tròn
 * @param  r       Bán kính (pixel)
 * @param  color   Màu
 */
void LCD_DrawCircle(uint8_t cx, uint8_t cy, uint8_t r, uint8_t color);

/* =====================================================================
 * API – Văn bản (font 5×7)
 * ===================================================================== */

/**
 * @brief  Hiển thị 1 ký tự ASCII với font 5×7
 * @param  x      Cột bắt đầu
 * @param  y      Hàng bắt đầu
 * @param  c      Ký tự ASCII
 * @param  color  LCD_COLOR_ON hoặc LCD_COLOR_OFF
 */
void LCD_DrawChar(uint8_t x, uint8_t y, char c, uint8_t color);

/**
 * @brief  Hiển thị chuỗi ký tự ASCII (font 5×7), tự xuống dòng
 * @param  x      Cột bắt đầu
 * @param  y      Hàng bắt đầu
 * @param  str    Chuỗi kết thúc '\0'
 * @param  color  Màu ký tự
 */
void LCD_DrawString(uint8_t x, uint8_t y, const char *str, uint8_t color);

/**
 * @brief  Hiển thị 1 ký tự ASCII phóng to theo tỉ lệ scale_x và scale_y
 */
void LCD_DrawCharScaled(uint8_t x, uint8_t y, char c, uint8_t scale_x, uint8_t scale_y, uint8_t color);

/**
 * @brief  Hiển thị chuỗi ký tự phóng to theo tỉ lệ scale_x và scale_y
 */
void LCD_DrawStringScaled(uint8_t x, uint8_t y, const char *str, uint8_t scale_x, uint8_t scale_y, uint8_t color);

/**
 * @brief  Hiển thị số nguyên thập phân
 * @param  x      Cột bắt đầu
 * @param  y      Hàng bắt đầu
 * @param  val    Giá trị cần hiển thị
 * @param  color  Màu ký tự
 */
void LCD_DrawInt(uint8_t x, uint8_t y, int32_t val, uint8_t color);

/* =====================================================================
 * API – Bitmap
 * ===================================================================== */

/**
 * @brief  Vẽ ảnh bitmap 1-bit lên framebuffer
 * @param  x, y   Góc trên-trái
 * @param  w, h   Kích thước ảnh
 * @param  bmp    Con trỏ mảng dữ liệu (mỗi byte = 8 pixel theo chiều ngang,
 *                MSB ở bên trái, hàng trước rồi đến hàng sau)
 */
void LCD_DrawBitmap(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *bmp);

/* =====================================================================
 * API – Phông chữ lớn (16×28 pixel, Arial Bold style)
 * ===================================================================== */

/**
 * @brief  Vẽ 1 ký tự bằng phông chữ lớn 16×28 (Arial Bold)
 * @param  x      Cột bắt đầu
 * @param  y      Hàng bắt đầu
 * @param  c      Ký tự cần vẽ ('0'-'9', '.', 'p', 'H', 'C', 'F')
 * @param  color  LCD_COLOR_ON hoặc LCD_COLOR_OFF
 * @return Độ rộng thực tế của ký tự vừa vẽ (pixel)
 */
uint8_t LCD_DrawLargeChar(uint8_t x, uint8_t y, char c, uint8_t color);

/**
 * @brief  Vẽ chuỗi ký tự bằng phông chữ lớn 16×28
 * @param  x      Cột bắt đầu
 * @param  y      Hàng bắt đầu
 * @param  str    Chuỗi ký tự kết thúc '\0'
 * @param  color  Màu ký tự
 */
void LCD_DrawLargeString(uint8_t x, uint8_t y, const char *str, uint8_t color);

/**
 * @brief  Tính độ rộng tổng (pixel) của chuỗi khi vẽ bằng phông chữ lớn
 * @param  str    Chuỗi ký tự cần tính
 * @return Tổng chiều rộng pixel (bao gồm khoảng cách 1px giữa các ký tự)
 */
uint8_t LCD_GetLargeStringWidth(const char *str);

uint8_t LCD_DrawMediumChar(uint8_t x, uint8_t y, char c, uint8_t color);
void LCD_DrawMediumString(uint8_t x, uint8_t y, const char *str, uint8_t color);
uint8_t LCD_GetMediumStringWidth(const char *str);

/* =====================================================================
 * API – FreeRTOS Task
 * ===================================================================== */

/**
 * @brief  Tạo FreeRTOS task demo vẽ màn hình định kỳ
 * @note   Gọi sau LCD_Init(). Task chạy trên core 1, ưu tiên 3.
 */
void screen_update_values(float ph, float temp);
void LCD_GetFramebuffer(uint8_t *dest);

void LCD_Start_Task(void);
void LCD_Pin_Diagnostics(void);

extern uint8_t g_lcd_contrast;
extern volatile bool g_lcd_need_redraw;

#endif /* MODULE_MAN_HINH_CAM_BIEN_H */
