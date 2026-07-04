/**
 * @file  menu.h
 * @brief Hệ thống menu phân cấp cho GMG12864-06D
 *
 *  Nút bấm:
 *   ESC   → GPIO 25  (back)
 *   DOWN  → GPIO 32  (lên dưới)
 *   UP    → GPIO 33  (lên trên)
 *   RIGHT → GPIO 34  (dự phòng)
 *   ENTER → GPIO 35  (xác nhận / vào mục)
 *
 *  Cây menu:
 *   [Đo lường] ──ENTER──▶ Main Menu
 *                          ├─ 1 System Settings
 *                          │   ├─ 1.1 Language → ENGLISH
 *                          │   ├─ 1.2 Date → Day Format | Day Settings
 *                          │   └─ 1.3 History Mode
 *                          ├─ 2 Sensor Settings
 *                          │   ├─ 2.1 Display Mode
 *                          │   ├─ 2.2 Calibration → Cal.2pt | Cal.3pt
 *                          │   ├─ 2.3 Digital Filter → L | M | H
 *                          │   ├─ 2.4 Temp Mode → ATC°C|MTC°C|ATF°F|MTF°F
 *                          │   ├─ 2.5 Temp Settings
 *                          │   └─ 2.6 Temp Lin COMP
 *                          └─ 3 Output Settings
 *                              ├─ 3.1 Relay-1 → Mode | SP | Hys
 *                              ├─ 3.2 Relay-2 → Mode | SP | Hys
 *                              ├─ 3.3 Relay-3 → Mode | SP | Hys
 *                              ├─ 3.4 Current-1 → Mode|Set4ma|Set20ma|Cal4ma|Cal20ma
 *                              └─ 3.5 ModBus RTU → Addr|Baud|Parity|StopBits
 */

#ifndef MENU_H
#define MENU_H

#include <stdint.h>
#include <stdbool.h>

/* =====================================================================
 * Ngôn ngữ hệ thống
 * ===================================================================== */
typedef enum {
    LANG_EN = 0,
    LANG_VI,
    LANG_COUNT
} sys_lang_t;

extern sys_lang_t g_sys_lang;

/* =====================================================================
 * Định dạng ngày hệ thống
 * ===================================================================== */
typedef enum {
    DATE_FORMAT_YYYY_MM_DD = 0,
    DATE_FORMAT_DD_MM_YYYY,
    DATE_FORMAT_MM_DD_YYYY,
    DATE_FORMAT_COUNT
} date_format_t;

extern date_format_t g_date_format;

/* =====================================================================
 * Che do hien thi man hinh chinh
 * ===================================================================== */
typedef enum {
    DISP_MODE_PH = 0,
    DISP_MODE_DO,
    DISP_MODE_DUAL,
    DISP_MODE_COUNT
} display_mode_t;

extern display_mode_t g_display_mode;

/* =====================================================================
 * Chân GPIO nút bấm
 * ===================================================================== */
#define BTN_PIN_ESC     13    /**< ESC   – quay lại */
#define BTN_PIN_DOWN    14    /**< DOWN  – xuống              */
#define BTN_PIN_UP      21    /**< UP    – lên                */
#define BTN_PIN_RIGHT   47    /**< RIGHT – dự phòng           */
#define BTN_PIN_ENTER   48    /**< ENTER – xác nhận           */

/* =====================================================================
 * ID trang menu
 * ===================================================================== */
typedef enum {
    PAGE_MEASUREMENT = 0,   /**< Màn hình đo lường (mặc định) */

    /* Level 1 */
    PAGE_MAIN_MENU,

    /* Level 2 */
    PAGE_SYSTEM_SETTINGS,
    PAGE_SENSOR_SETTINGS,
    PAGE_MODBUS_SETTINGS,

    /* Level 3 – System Settings */
    PAGE_LANGUAGE,
    PAGE_DATE,
    PAGE_DATE_FORMAT,
    PAGE_TIME_SETTINGS,
    PAGE_DISPLAY_MODE,
    PAGE_SCREEN_SETTINGS,
    PAGE_SCREEN_CONTRAST,
    PAGE_SCREEN_RES_RATIO,

    /* Level 3 – Sensor Settings */
    PAGE_CALIBRATION,
    PAGE_CAL_2PT,
    PAGE_CAL_3PT,
    PAGE_CAL_3PT_G1,
    PAGE_CAL_3PT_G2,
    PAGE_CAL_EXEC,
    PAGE_CAL_DO,
    PAGE_CAL_DO_EXEC,
    PAGE_CAL_DO_TEMP,
    PAGE_DIGITAL_FILTER,
    PAGE_TEMP_MODE,
    PAGE_TEMP_SETTINGS,
    PAGE_TEMP_LIN_COMP,

    /* Level 3 – Modbus Settings */
    PAGE_MODBUS_PORT1,
    PAGE_MODBUS_PORT2,
    PAGE_MODBUS_EDIT_ADDR,
    PAGE_MODBUS_SELECT_BAUD,
    PAGE_MODBUS_SELECT_PARITY,
    PAGE_MODBUS_SELECT_STOP,

    PAGE_LEAF,              /**< Mục lá – không có trang con   */
    PAGE_COUNT
} menu_page_t;

/* =====================================================================
 * Trạng thái menu toàn cục
 * ===================================================================== */
typedef struct {
    menu_page_t current_page;   /**< Trang đang hiển thị           */
    uint8_t     selected;       /**< Chỉ số mục đang được chọn     */
    uint8_t     scroll_offset;  /**< Chỉ số mục đầu tiên hiển thị  */
    bool        in_menu;        /**< false = màn hình đo lường      */
} menu_state_t;

extern menu_state_t g_menu;

/* =====================================================================
 * API
 * ===================================================================== */

/** @brief Khởi tạo GPIO các nút bấm */
void menu_init(void);

void menu_load_settings(void);

/**
 * @brief  Đọc trạng thái nút bấm và cập nhật trạng thái menu
 * @note   Gọi mỗi 50 ms trong task loop
 */
void menu_handle_buttons(void);

/** @brief Vẽ màn hình menu hiện tại lên LCD và Flush */
void menu_render(void);

/**
 * @brief  Gia lap nhan nut tu xa (phuc vu mo phong tren Web Server)
 * @param  btn_name Ten nut bam ("esc", "down", "up", "right", "enter")
 * @return true neu ten nut hop le, false neu nguoc lai
 */
bool menu_simulate_press(const char *btn_name);

#endif /* MENU_H */