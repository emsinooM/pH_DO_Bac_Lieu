/**
 * @file  menu.c
 * @brief Triển khai hệ thống menu phân cấp cho GMG12864-06D
 *
 *  Layout màn hình 128×64 (khi hiển thị menu):
 *   y =  0..10  → Title bar (nền đen, chữ trắng) – 11 px
 *   y = 11..51  → Danh sách mục (5 hàng × 8 px)
 *   y = 52..63  → Status bar (nền đen, chữ trắng) – 12 px
 *
 *  Phím điều hướng:
 *   UP / DOWN  → chọn mục (có scroll nếu > 5 mục)
 *   ENTER      → vào mục con (hoặc thực thi mục lá)
 *   ESC        → quay lại trang cha
 */

#include "screen_menu.h"
#include "screen_disp.h"
#include "user_storage.h"
#include "filter.h"
#include "ph_temp.h"
#include "do_sensor.h"
#include "ds3231.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG_MENU = "MENU";

/* =====================================================================
 * Trạng thái menu (định nghĩa biến toàn cục)
 * ===================================================================== */
menu_state_t g_menu = {
    .current_page   = PAGE_MEASUREMENT,
    .selected       = 0,
    .scroll_offset  = 0,
    .in_menu        = false,
    .in_pin_entry   = false,
};

/* Định nghĩa biến cấu hình toàn cục */
sys_lang_t     g_sys_lang     = LANG_EN;
date_format_t  g_date_format  = DATE_FORMAT_YYYY_MM_DD;
display_mode_t g_display_mode = DISP_MODE_PH;
display_view_t g_display_view = DISP_VIEW_NUMBER;

/* =====================================================================
 * Cấu trúc mô tả một trang menu
 * ===================================================================== */
#define MAX_ITEMS  8   /**< Số mục tối đa trong 1 trang */

typedef struct {
    const char    *title;                   /**< Tiêu đề trang                */
    uint8_t        item_count;              /**< Số lượng mục                  */
    const char    *items[MAX_ITEMS];        /**< Nhãn các mục                  */
    menu_page_t    children[MAX_ITEMS];     /**< Trang con (PAGE_LEAF nếu lá)  */
    menu_page_t    parent;                  /**< Trang cha                     */
} page_def_t;

/* =====================================================================
 * Cây menu toàn bộ
 * ===================================================================== */
static const page_def_t s_pages_en[PAGE_COUNT] = {

    /* ── Main Menu ── */
    [PAGE_MAIN_MENU] = {
        .title      = "Main Menu",
        .item_count = 4,
        .items      = { "1 System Settings",
                        "2 Display Settings",
                        "3 Modbus Settings",
                        "4 Sensor Settings" },
        .children   = { PAGE_SYSTEM_SETTINGS,
                        PAGE_DISPLAY_MODE,
                        PAGE_MODBUS_SETTINGS,
                        PAGE_SENSOR_SETTINGS },
        .parent     = PAGE_MEASUREMENT,
    },

    /* ── System Settings ── */
    [PAGE_SYSTEM_SETTINGS] = {
        .title      = "System Settings",
        .item_count = 3,
        .items      = { "1.1 Language",
                        "1.2 Date",
                        "1.3 Screen Settings" },
        .children   = { PAGE_LANGUAGE, PAGE_DATE, PAGE_SCREEN_SETTINGS },
        .parent     = PAGE_MAIN_MENU,
    },

    [PAGE_SCREEN_SETTINGS] = {
        .title      = "Screen Settings",
        .item_count = 2,
        .items      = { "1.3.1 Contrast",
                        "1.3.2 Resistor Ratio" },
        .children   = { PAGE_SCREEN_CONTRAST, PAGE_SCREEN_RES_RATIO },
        .parent     = PAGE_SYSTEM_SETTINGS,
    },

    [PAGE_SCREEN_CONTRAST] = {
        .title      = "Contrast Settings",
        .item_count = 0,
        .parent     = PAGE_SCREEN_SETTINGS,
    },

    [PAGE_SCREEN_RES_RATIO] = {
        .title      = "Resistor Ratio",
        .item_count = 0,
        .parent     = PAGE_SCREEN_SETTINGS,
    },

    [PAGE_LANGUAGE] = {
        .title      = "Language",
        .item_count = 2,
        .items      = { "1.1.1 ENGLISH",
                        "1.1.2 VIETNAMESE" },
        .children   = { PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_SYSTEM_SETTINGS,
    },

    [PAGE_DATE] = {
        .title      = "Date",
        .item_count = 2,
        .items      = { "1.2.1 Day Format",
                        "1.2.2 Time Settings" },
        .children   = { PAGE_DATE_FORMAT, PAGE_TIME_SETTINGS },
        .parent     = PAGE_SYSTEM_SETTINGS,
    },

    [PAGE_DATE_FORMAT] = {
        .title      = "Day Format",
        .item_count = 3,
        .items      = { "1.2.1.1 YYYY-MM-DD",
                        "1.2.1.2 DD-MM-YYYY",
                        "1.2.1.3 MM-DD-YYYY" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_DATE,
    },

    /* ── Display Settings (menu 2) ── */
    [PAGE_DISPLAY_MODE] = {
        .title      = "Display Settings",
        .item_count = 3,
        .items      = { "2.1 pH Mode",
                        "2.2 DO Mode",
                        "2.3 Dual Mode" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_MAIN_MENU,
    },

    /* ── Sensor Settings (menu 4) ── */
    [PAGE_SENSOR_SETTINGS] = {
        .title      = "Sensor Settings",
        .item_count = 5,
        .items      = { "4.1 Calibration",
                        "4.2 Digital Filter",
                        "4.3 Temp Mode",
                        "4.4 Temp Settings",
                        "4.5 Temp Lin COMP" },
        .children   = { PAGE_CALIBRATION,
                        PAGE_DIGITAL_FILTER,
                        PAGE_TEMP_MODE,
                        PAGE_TEMP_SETTINGS,
                        PAGE_TEMP_LIN_COMP },
        .parent     = PAGE_MAIN_MENU,
    },

    [PAGE_CALIBRATION] = {
        .title      = "Calibration",
        .item_count = 3,
        .items      = { "4.1.1 Cal. 2 point",
                        "4.1.2 Cal. 3 point",
                        "4.1.3 Cal. DO" },
        .children   = { PAGE_CAL_2PT, PAGE_CAL_3PT, PAGE_CAL_DO },
        .parent     = PAGE_SENSOR_SETTINGS,
    },

    [PAGE_CAL_DO] = {
        .title      = "DO Calibration",
        .item_count = 4,
        .items      = { "1. Cal. DO Zero",
                        "2. Cal. DO Slope",
                        "3. Cal. DO Temp",
                        "4. Reset Sensor" },
        .children   = { PAGE_CAL_DO_EXEC, PAGE_CAL_DO_EXEC, PAGE_CAL_DO_TEMP, PAGE_LEAF },
        .parent     = PAGE_CALIBRATION,
    },

    [PAGE_CAL_2PT] = {
        .title      = "Cal. 2 point",
        .item_count = 2,
        .items      = { "1. Low Cal. 4.00",
                        "2. High Cal. 7.00" },
        .children   = { PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_CALIBRATION,
    },

    [PAGE_CAL_3PT] = {
        .title      = "Cal. 3 point",
        .item_count = 2,
        .items      = { "1. Group 1(4/6/9)",
                        "2. Group 2(4/7/10)" },
        .children   = { PAGE_CAL_3PT_G1, PAGE_CAL_3PT_G2 },
        .parent     = PAGE_CALIBRATION,
    },

    [PAGE_CAL_3PT_G1] = {
        .title      = "Group 1(4/6/9)",
        .item_count = 3,
        .items      = { "1. Low Cal. 4.00",
                        "2. Mid Cal. 6.86",
                        "3. High Cal. 9.18" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_CAL_3PT,
    },

    [PAGE_CAL_3PT_G2] = {
        .title      = "Group 2(4/7/10)",
        .item_count = 3,
        .items      = { "1. Low Cal. 4.00",
                        "2. Mid Cal. 7.00",
                        "3. High Cal. 10.00" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_CAL_3PT,
    },

    [PAGE_DIGITAL_FILTER] = {
        .title      = "Digital Filter",
        .item_count = 3,
        .items      = { "4.2.1 L",
                        "4.2.2 M",
                        "4.2.3 H" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_SENSOR_SETTINGS,
    },

    [PAGE_TEMP_MODE] = {
        .title      = "Temp Mode",
        .item_count = 4,
        .items      = { "4.3.1 ATC  C",
                        "4.3.2 MTC  C",
                        "4.3.3 ATF  F",
                        "4.3.4 MTF  F" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_SENSOR_SETTINGS,
    },

    [PAGE_TEMP_SETTINGS] = {
        .title      = "Temp Settings",
        .item_count = 0,
        .parent     = PAGE_SENSOR_SETTINGS,
    },

    [PAGE_TEMP_LIN_COMP] = {
        .title      = "Temp Lin COMP",
        .item_count = 0,
        .parent     = PAGE_SENSOR_SETTINGS,
    },

    /* ── Output Settings ── */
    [PAGE_MODBUS_SETTINGS] = {
        .title      = "Modbus Settings",
        .item_count = 2,
        .items      = { "3.1 Modbus Port 1 (Ext)",
                        "3.2 Modbus Port 2 (DO)" },
        .children   = { PAGE_MODBUS_PORT1,
                        PAGE_MODBUS_PORT2 },
        .parent     = PAGE_MAIN_MENU,
    },

    [PAGE_MODBUS_PORT1] = {
        .title      = "Modbus Port 1",
        .item_count = 4,
        .items      = { "3.1.1 MB Address",
                        "3.1.2 Baud Rate",
                        "3.1.3 Parity Check",
                        "3.1.4 Stop Bits" },
        .children   = { PAGE_MODBUS_EDIT_ADDR,
                        PAGE_MODBUS_SELECT_BAUD,
                        PAGE_MODBUS_SELECT_PARITY,
                        PAGE_MODBUS_SELECT_STOP },
        .parent     = PAGE_MODBUS_SETTINGS,
    },

    [PAGE_MODBUS_PORT2] = {
        .title      = "Modbus Port 2",
        .item_count = 4,
        .items      = { "3.2.1 MB Address",
                        "3.2.2 Baud Rate",
                        "3.2.3 Parity Check",
                        "3.2.4 Stop Bits" },
        .children   = { PAGE_MODBUS_EDIT_ADDR,
                        PAGE_MODBUS_SELECT_BAUD,
                        PAGE_MODBUS_SELECT_PARITY,
                        PAGE_MODBUS_SELECT_STOP },
        .parent     = PAGE_MODBUS_SETTINGS,
    },

    [PAGE_MODBUS_SELECT_BAUD] = {
        .title      = "Baud Rate",
        .item_count = 7,
        .items      = { "2400", "4800", "9600", "19200", "38400", "57600", "115200" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF, PAGE_LEAF, PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_MODBUS_PORT1,
    },

    [PAGE_MODBUS_SELECT_PARITY] = {
        .title      = "Parity Check",
        .item_count = 3,
        .items      = { "None", "Even", "Odd" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_MODBUS_PORT1,
    },

    [PAGE_MODBUS_SELECT_STOP] = {
        .title      = "Stop Bits",
        .item_count = 2,
        .items      = { "1 Stop Bit", "2 Stop Bits" },
        .children   = { PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_MODBUS_PORT1,
    },

    [PAGE_TIME_SETTINGS] = {
        .title      = "Time Settings",
        .item_count = 0,
        .parent     = PAGE_DATE,
    },

    [PAGE_CAL_EXEC] = {
        .title      = "Calibration Exec",
        .item_count = 0,
        .parent     = PAGE_CALIBRATION,
    },

    [PAGE_CAL_DO_EXEC] = {
        .title      = "DO Calibration Exec",
        .item_count = 0,
        .parent     = PAGE_CAL_DO,
    },

    [PAGE_CAL_DO_TEMP] = {
        .title      = "DO Temp Cal",
        .item_count = 0,
        .parent     = PAGE_CAL_DO,
    },

    [PAGE_MODBUS_EDIT_ADDR] = {
        .title      = "Modbus Address",
        .item_count = 0,
        .parent     = PAGE_MODBUS_PORT1,
    },
};

static const page_def_t s_pages_vi[PAGE_COUNT] = {

    /* ── Main Menu ── */
    [PAGE_MAIN_MENU] = {
        .title      = "Menu Chinh",
        .item_count = 4,
        .items      = { "1 Cai Dat He Thong",
                        "2 Cai Dat Hien Thi",
                        "3 Cai Dat Modbus",
                        "4 Cai Dat Cam Bien" },
        .children   = { PAGE_SYSTEM_SETTINGS,
                        PAGE_DISPLAY_MODE,
                        PAGE_MODBUS_SETTINGS,
                        PAGE_SENSOR_SETTINGS },
        .parent     = PAGE_MEASUREMENT,
    },

    /* ── System Settings ── */
    [PAGE_SYSTEM_SETTINGS] = {
        .title      = "Cai Dat He Thong",
        .item_count = 3,
        .items      = { "1.1 Ngon Ngu",
                        "1.2 Ngay Thang",
                        "1.3 Cai Dat Man Hinh" },
        .children   = { PAGE_LANGUAGE, PAGE_DATE, PAGE_SCREEN_SETTINGS },
        .parent     = PAGE_MAIN_MENU,
    },

    [PAGE_SCREEN_SETTINGS] = {
        .title      = "Cai Dat Man Hinh",
        .item_count = 2,
        .items      = { "1.3.1 Tuong Phan",
                        "1.3.2 Ty So Dien Tro" },
        .children   = { PAGE_SCREEN_CONTRAST, PAGE_SCREEN_RES_RATIO },
        .parent     = PAGE_SYSTEM_SETTINGS,
    },

    [PAGE_SCREEN_CONTRAST] = {
        .title      = "Cai Dat Tuong Phan",
        .item_count = 0,
        .parent     = PAGE_SCREEN_SETTINGS,
    },

    [PAGE_SCREEN_RES_RATIO] = {
        .title      = "Ty So Dien Tro",
        .item_count = 0,
        .parent     = PAGE_SCREEN_SETTINGS,
    },

    [PAGE_LANGUAGE] = {
        .title      = "Ngon Ngu",
        .item_count = 2,
        .items      = { "1.1.1 TIENG ANH",
                        "1.1.2 TIENG VIET" },
        .children   = { PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_SYSTEM_SETTINGS,
    },

    [PAGE_DATE] = {
        .title      = "Ngay Thang",
        .item_count = 2,
        .items      = { "1.2.1 Dinh Dang Ngay",
                        "1.2.2 Cai Dat Gio" },
        .children   = { PAGE_DATE_FORMAT, PAGE_TIME_SETTINGS },
        .parent     = PAGE_SYSTEM_SETTINGS,
    },

    [PAGE_DATE_FORMAT] = {
        .title      = "Dinh Dang Ngay",
        .item_count = 3,
        .items      = { "1.2.1.1 YYYY-MM-DD",
                        "1.2.1.2 DD-MM-YYYY",
                        "1.2.1.3 MM-DD-YYYY" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_DATE,
    },

    /* ── Display Settings (menu 2) ── */
    [PAGE_DISPLAY_MODE] = {
        .title      = "Cai Dat Hien Thi",
        .item_count = 3,
        .items      = { "2.1 Che Do pH",
                        "2.2 Che Do DO",
                        "2.3 Che Do Song Song" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_MAIN_MENU,
    },

    /* ── Sensor Settings (menu 4) ── */
    [PAGE_SENSOR_SETTINGS] = {
        .title      = "Cai Dat Cam Bien",
        .item_count = 5,
        .items      = { "4.1 Hieu Chuan",
                        "4.2 Bo Loc So",
                        "4.3 Che Do Nhiet Do",
                        "4.4 Cai Dat Nhiet Do",
                        "4.5 Bu Tuyen Tinh T" },
        .children   = { PAGE_CALIBRATION,
                        PAGE_DIGITAL_FILTER,
                        PAGE_TEMP_MODE,
                        PAGE_TEMP_SETTINGS,
                        PAGE_TEMP_LIN_COMP },
        .parent     = PAGE_MAIN_MENU,
    },

    [PAGE_CALIBRATION] = {
        .title      = "Hieu Chuan",
        .item_count = 3,
        .items      = { "4.1.1 Hieu Chuan 2D",
                        "4.1.2 Hieu Chuan 3D",
                        "4.1.3 Hieu Chuan DO" },
        .children   = { PAGE_CAL_2PT, PAGE_CAL_3PT, PAGE_CAL_DO },
        .parent     = PAGE_SENSOR_SETTINGS,
    },

    [PAGE_CAL_DO] = {
        .title      = "Hieu Chuan DO",
        .item_count = 4,
        .items      = { "1. Hieu Chuan Diem 0",
                        "2. Hieu Chuan Do Doc",
                        "3. Hieu Chinh Nhiet Do",
                        "4. Reset Cam Bien" },
        .children   = { PAGE_CAL_DO_EXEC, PAGE_CAL_DO_EXEC, PAGE_CAL_DO_TEMP, PAGE_LEAF },
        .parent     = PAGE_CALIBRATION,
    },

    [PAGE_CAL_2PT] = {
        .title      = "Hieu Chuan 2D",
        .item_count = 2,
        .items      = { "1. Cal. Thap 4.00",
                        "2. Cal. Cao 7.00" },
        .children   = { PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_CALIBRATION,
    },

    [PAGE_CAL_3PT] = {
        .title      = "Hieu Chuan 3D",
        .item_count = 2,
        .items      = { "1. Nhom 1(4/6/9)",
                        "2. Nhom 2(4/7/10)" },
        .children   = { PAGE_CAL_3PT_G1, PAGE_CAL_3PT_G2 },
        .parent     = PAGE_CALIBRATION,
    },

    [PAGE_CAL_3PT_G1] = {
        .title      = "Nhom 1(4/6/9)",
        .item_count = 3,
        .items      = { "1. Cal. Thap 4.00",
                        "2. Cal. Trung 6.86",
                        "3. Cal. Cao 9.18" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_CAL_3PT,
    },

    [PAGE_CAL_3PT_G2] = {
        .title      = "Nhom 2(4/7/10)",
        .item_count = 3,
        .items      = { "1. Cal. Thap 4.00",
                        "2. Cal. Trung 7.00",
                        "3. Cal. Cao 10.00" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_CAL_3PT,
    },

    [PAGE_DIGITAL_FILTER] = {
        .title      = "Bo Loc So",
        .item_count = 3,
        .items      = { "4.2.1 Thap",
                        "4.2.2 Vua",
                        "4.2.3 Cao" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_SENSOR_SETTINGS,
    },

    [PAGE_TEMP_MODE] = {
        .title      = "Che Do Nhiet Do",
        .item_count = 4,
        .items      = { "4.3.1 ATC  C",
                        "4.3.2 MTC  C",
                        "4.3.3 ATF  F",
                        "4.3.4 MTF  F" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_SENSOR_SETTINGS,
    },

    [PAGE_TEMP_SETTINGS] = {
        .title      = "Cai Dat Nhiet Do",
        .item_count = 0,
        .parent     = PAGE_SENSOR_SETTINGS,
    },

    [PAGE_TEMP_LIN_COMP] = {
        .title      = "Bu Tuyen Tinh T",
        .item_count = 0,
        .parent     = PAGE_SENSOR_SETTINGS,
    },

    /* ── Output Settings ── */
    [PAGE_MODBUS_SETTINGS] = {
        .title      = "Cai Dat Modbus",
        .item_count = 2,
        .items      = { "3.1 Cong Modbus 1 (MR)",
                        "3.2 Cong Modbus 2 (DO)" },
        .children   = { PAGE_MODBUS_PORT1,
                        PAGE_MODBUS_PORT2 },
        .parent     = PAGE_MAIN_MENU,
    },

    [PAGE_MODBUS_PORT1] = {
        .title      = "Cong Modbus 1",
        .item_count = 4,
        .items      = { "3.1.1 Dia Chi MB",
                        "3.1.2 Toc Do Baud",
                        "3.1.3 Kiem Tra Parity",
                        "3.1.4 Bit Stop" },
        .children   = { PAGE_MODBUS_EDIT_ADDR,
                        PAGE_MODBUS_SELECT_BAUD,
                        PAGE_MODBUS_SELECT_PARITY,
                        PAGE_MODBUS_SELECT_STOP },
        .parent     = PAGE_MODBUS_SETTINGS,
    },

    [PAGE_MODBUS_PORT2] = {
        .title      = "Cong Modbus 2",
        .item_count = 4,
        .items      = { "3.2.1 Dia Chi MB",
                        "3.2.2 Toc Do Baud",
                        "3.2.3 Kiem Tra Parity",
                        "3.2.4 Bit Stop" },
        .children   = { PAGE_MODBUS_EDIT_ADDR,
                        PAGE_MODBUS_SELECT_BAUD,
                        PAGE_MODBUS_SELECT_PARITY,
                        PAGE_MODBUS_SELECT_STOP },
        .parent     = PAGE_MODBUS_SETTINGS,
    },

    [PAGE_MODBUS_SELECT_BAUD] = {
        .title      = "Toc Do Baud",
        .item_count = 7,
        .items      = { "2400", "4800", "9600", "19200", "38400", "57600", "115200" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF, PAGE_LEAF, PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_MODBUS_PORT1,
    },

    [PAGE_MODBUS_SELECT_PARITY] = {
        .title      = "Kiem Tra Parity",
        .item_count = 3,
        .items      = { "Khong", "Chan", "Le" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_MODBUS_PORT1,
    },

    [PAGE_MODBUS_SELECT_STOP] = {
        .title      = "Bit Stop",
        .item_count = 2,
        .items      = { "1 Bit Stop", "2 Bit Stop" },
        .children   = { PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_MODBUS_PORT1,
    },

    [PAGE_TIME_SETTINGS] = {
        .title      = "Cai Dat Gio",
        .item_count = 0,
        .parent     = PAGE_DATE,
    },

    [PAGE_CAL_EXEC] = {
        .title      = "Thuc Hien Hieu Chuan",
        .item_count = 0,
        .parent     = PAGE_CALIBRATION,
    },

    [PAGE_CAL_DO_EXEC] = {
        .title      = "Thuc Hien Cal DO",
        .item_count = 0,
        .parent     = PAGE_CAL_DO,
    },

    [PAGE_CAL_DO_TEMP] = {
        .title      = "Cal Nhiet Do DO",
        .item_count = 0,
        .parent     = PAGE_CAL_DO,
    },

    [PAGE_MODBUS_EDIT_ADDR] = {
        .title      = "Dia Chi Modbus",
        .item_count = 0,
        .parent     = PAGE_MODBUS_PORT1,
    },
};

/* =====================================================================
 * Điều hướng nội bộ
 * ===================================================================== */
#define VISIBLE_ITEMS   4    /**< Số hàng mục hiển thị cùng lúc */
#define ITEM_ROW_H      9    /**< Chiều cao mỗi hàng (px)        */
#define TITLE_BAR_H     11   /**< Chiều cao title bar (px)       */
#define STATUS_BAR_H    13   /**< Chiều cao status bar (px)      */
#define ITEMS_Y_START   13   /**< y đầu tiên của danh sách       */
#define STATUS_BAR_Y    51   /**< y bắt đầu status bar           */

#define MENU_PIN_LEN        4
#define MENU_PIN_DEFAULT    "1234"

static char    s_menu_pin_stored[MENU_PIN_LEN + 1] = MENU_PIN_DEFAULT;
static uint8_t s_pin_entry[MENU_PIN_LEN] = {0, 0, 0, 0};
static uint8_t s_pin_cursor = 0;
static bool    s_pin_show_error = false;
static menu_page_t s_pin_target_page = PAGE_MODBUS_SETTINGS;
static uint8_t s_pin_reveal = 0;   /**< so lan ve con lai de hien so truoc khi an */

#define MENU_PIN_REVEAL_TICKS 16   /**< ~0.8s (16 x 50ms) hien so roi an thanh * */

/* =====================================================================
 * Cai dat gio thu cong (PAGE_TIME_SETTINGS)
 *   Truong: 0=nam 1=thang 2=ngay 3=gio 4=phut 5=giay
 * ===================================================================== */
#define TIME_FIELD_COUNT 6
static struct tm s_time_edit;          /**< buffer dang chinh */
static uint8_t   s_time_field = 0;     /**< truong dang chon */
static bool      s_time_saved_msg = false; /**< vua luu thanh cong */

static uint8_t time_days_in_month(int year1900, int mon0)
{
    static const uint8_t dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (mon0 == 1) { /* thang 2 */
        int y = year1900 + 1900;
        bool leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
        return leap ? 29 : 28;
    }
    return dim[mon0];
}

static void time_edit_begin(void)
{
    time_t now = time(NULL);
    localtime_r(&now, &s_time_edit);
    if (s_time_edit.tm_year < 100) {   /* chua co gio -> mac dinh 2026-01-01 00:00:00 */
        s_time_edit.tm_year = 126;
        s_time_edit.tm_mon  = 0;
        s_time_edit.tm_mday = 1;
        s_time_edit.tm_hour = 0;
        s_time_edit.tm_min  = 0;
        s_time_edit.tm_sec  = 0;
    }
    s_time_field = 0;
    s_time_saved_msg = false;
}

static void time_edit_clamp_day(void)
{
    uint8_t maxd = time_days_in_month(s_time_edit.tm_year, s_time_edit.tm_mon);
    if (s_time_edit.tm_mday < 1) s_time_edit.tm_mday = maxd;
    if (s_time_edit.tm_mday > maxd) s_time_edit.tm_mday = 1;
}

static void time_edit_adjust(int8_t dir)
{
    switch (s_time_field) {
    case 0: /* nam 2000..2099 (tm_year 100..199) */
        s_time_edit.tm_year += dir;
        if (s_time_edit.tm_year < 100) s_time_edit.tm_year = 199;
        if (s_time_edit.tm_year > 199) s_time_edit.tm_year = 100;
        time_edit_clamp_day();
        break;
    case 1: /* thang 0..11 */
        s_time_edit.tm_mon += dir;
        if (s_time_edit.tm_mon < 0) s_time_edit.tm_mon = 11;
        if (s_time_edit.tm_mon > 11) s_time_edit.tm_mon = 0;
        time_edit_clamp_day();
        break;
    case 2: { /* ngay 1..maxd */
        uint8_t maxd = time_days_in_month(s_time_edit.tm_year, s_time_edit.tm_mon);
        s_time_edit.tm_mday += dir;
        if (s_time_edit.tm_mday < 1) s_time_edit.tm_mday = maxd;
        if (s_time_edit.tm_mday > maxd) s_time_edit.tm_mday = 1;
        break;
    }
    case 3: /* gio 0..23 */
        s_time_edit.tm_hour += dir;
        if (s_time_edit.tm_hour < 0) s_time_edit.tm_hour = 23;
        if (s_time_edit.tm_hour > 23) s_time_edit.tm_hour = 0;
        break;
    case 4: /* phut 0..59 */
        s_time_edit.tm_min += dir;
        if (s_time_edit.tm_min < 0) s_time_edit.tm_min = 59;
        if (s_time_edit.tm_min > 59) s_time_edit.tm_min = 0;
        break;
    case 5: /* giay 0..59 */
        s_time_edit.tm_sec += dir;
        if (s_time_edit.tm_sec < 0) s_time_edit.tm_sec = 59;
        if (s_time_edit.tm_sec > 59) s_time_edit.tm_sec = 0;
        break;
    default: break;
    }
}

static void time_edit_save(void)
{
    struct tm t = s_time_edit;
    t.tm_isdst = -1;
    time_t epoch = mktime(&t);
    if (epoch == (time_t)-1) {
        return;
    }
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    /* Ghi vao RTC DS3231 de giu gio khi mat dien */
    if (ds3231_set_time(&s_time_edit) == ESP_OK) {
        ds3231_clear_oscillator_flag();
        ESP_LOGI(TAG_MENU, "Da set gio thu cong: %04d-%02d-%02d %02d:%02d:%02d",
                 s_time_edit.tm_year + 1900, s_time_edit.tm_mon + 1,
                 s_time_edit.tm_mday, s_time_edit.tm_hour, s_time_edit.tm_min,
                 s_time_edit.tm_sec);
    } else {
        ESP_LOGW(TAG_MENU, "Set gio he thong OK nhung ghi RTC that bai");
    }
    s_time_saved_msg = true;
}

static void menu_pin_load(void)
{
    char pin[MENU_PIN_LEN + 1] = {0};
    if (Nvs_Read_String("menu_pin", pin) && strlen(pin) == MENU_PIN_LEN) {
        memcpy(s_menu_pin_stored, pin, MENU_PIN_LEN + 1);
    } else {
        strncpy(s_menu_pin_stored, MENU_PIN_DEFAULT, sizeof(s_menu_pin_stored));
    }
}

static void menu_pin_begin_entry(menu_page_t target)
{
    memset(s_pin_entry, 0, sizeof(s_pin_entry));
    s_pin_cursor = 0;
    s_pin_show_error = false;
    s_pin_reveal = 0;
    s_pin_target_page = target;
    g_menu.in_pin_entry = true;
}

static void menu_pin_handle_buttons(void);
static void menu_pin_render(void);

static bool menu_pin_verify(void)
{
    char entered[MENU_PIN_LEN + 1];
    for (uint8_t i = 0; i < MENU_PIN_LEN; i++) {
        entered[i] = (char)('0' + s_pin_entry[i]);
    }
    entered[MENU_PIN_LEN] = '\0';
    return strcmp(entered, s_menu_pin_stored) == 0;
}

static void goto_page(menu_page_t page)
{
    g_menu.current_page  = page;
    g_menu.selected      = 0;
    g_menu.scroll_offset = 0;
    if (page == PAGE_TIME_SETTINGS) {
        time_edit_begin();     /* nap gio hien tai vao buffer chinh sua */
    }
    ESP_LOGI(TAG_MENU, "Navigate -> page %d", (int)page);
}

/* =====================================================================
 * Debounce nút bấm (edge-detect, polling 50 ms)
 * ===================================================================== */
typedef enum {
    BTN_IDX_ESC   = 0,
    BTN_IDX_DOWN  = 1,
    BTN_IDX_UP    = 2,
    BTN_IDX_RIGHT = 3,
    BTN_IDX_ENTER = 4,
    BTN_COUNT
} btn_idx_t;

static const uint8_t s_btn_gpios[BTN_COUNT] = {
    BTN_PIN_ESC,
    BTN_PIN_DOWN,
    BTN_PIN_UP,
    BTN_PIN_RIGHT,
    BTN_PIN_ENTER,
};

static uint8_t s_btn_debounce_counter[BTN_COUNT] = {0};
static bool    s_btn_state[BTN_COUNT] = {false};
static bool    s_btn_prev_state[BTN_COUNT] = {false};
static volatile bool s_btn_simulated[BTN_COUNT] = {false};

/**
 * @brief  Đọc trạng thái GPIO và chống rung (debounce) cho các nút bấm
 *         Yêu cầu trạng thái ổn định trong 2 chu kỳ quét liên tiếp (100 ms)
 */
static void poll_and_debounce_buttons(void)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        // Lưu trạng thái trước đó của chu kỳ trước
        s_btn_prev_state[i] = s_btn_state[i];

        // Đọc trạng thái vật lý (0 = Đang nhấn/LOW, 1 = Thả/HIGH)
        bool raw = (gpio_get_level(s_btn_gpios[i]) == 0);

        if (raw != s_btn_state[i]) {
            s_btn_debounce_counter[i]++;
            if (s_btn_debounce_counter[i] >= 2) {
                s_btn_state[i] = raw;
                s_btn_debounce_counter[i] = 0;
            }
        } else {
            s_btn_debounce_counter[i] = 0;
        }
    }
}

/**
 * @brief  Trả về true nếu nút vừa được nhấn (phát hiện sườn lên đã chống rung)
 */
static bool btn_edge(btn_idx_t idx)
{
    if (s_btn_simulated[idx]) {
        s_btn_simulated[idx] = false;
        return true;
    }
    return s_btn_state[idx] && !s_btn_prev_state[idx];
}

/* =====================================================================
 * Khởi tạo GPIO nút bấm
 * ===================================================================== */
void menu_load_settings(void)
{
    uint32_t val;
    if (Nvs_Read_Number("sys_lang", &val)) {
        g_sys_lang = (sys_lang_t)val;
    }
    if (Nvs_Read_Number("date_format", &val)) {
        g_date_format = (date_format_t)val;
    }
    if (Nvs_Read_Number("disp_mode", &val)) {
        g_display_mode = (display_mode_t)val;
    }
    if (Nvs_Read_Number("disp_view", &val) && val < DISP_VIEW_COUNT) {
        g_display_view = (display_view_t)val;
    }
    if (Nvs_Read_Number("lcd_contrast", &val)) {
        g_lcd_contrast = (uint8_t)val;
        LCD_SetContrast(g_lcd_contrast);
    }
    if (Nvs_Read_Number("lcd_res_ratio", &val)) {
        g_lcd_resistor_ratio = (uint8_t)val;
        LCD_SetResistorRatio(g_lcd_resistor_ratio);
    }
    if (Nvs_Read_Number("filter_lvl", &val)) {
        g_filter_level = (filter_level_t)val;
        update_system_filters_level(g_filter_level);
    }
    menu_pin_load();
}

static void menu_handle_leaf_select(void)
{
    menu_page_t cur = g_menu.current_page;
    uint8_t sel = g_menu.selected;

    if (cur == PAGE_LANGUAGE) {
        g_sys_lang = (sys_lang_t)sel;
        Nvs_Write_Number("sys_lang", (uint32_t)g_sys_lang);
        ESP_LOGI(TAG_MENU, "Luu ngon ngu NVS: %d", g_sys_lang);
        g_lcd_need_redraw = true;
        goto_page(PAGE_SYSTEM_SETTINGS);
    }
    else if (cur == PAGE_DATE_FORMAT) {
        g_date_format = (date_format_t)sel;
        Nvs_Write_Number("date_format", (uint32_t)g_date_format);
        ESP_LOGI(TAG_MENU, "Luu dinh dang ngay NVS: %d", g_date_format);
        g_lcd_need_redraw = true;
        goto_page(PAGE_DATE);
    }
    else if (cur == PAGE_DISPLAY_MODE) {
        g_display_mode = (display_mode_t)sel;
        Nvs_Write_Number("disp_mode", (uint32_t)g_display_mode);
        ESP_LOGI(TAG_MENU, "Luu che do hien thi NVS: %d", g_display_mode);
        g_lcd_need_redraw = true;
        goto_page(PAGE_MAIN_MENU);
    }
    else if (cur == PAGE_DIGITAL_FILTER) {
        g_filter_level = (filter_level_t)sel;
        Nvs_Write_Number("filter_lvl", (uint32_t)g_filter_level);
        update_system_filters_level(g_filter_level);
        ESP_LOGI(TAG_MENU, "Luu bo loc so NVS: %d", g_filter_level);
        g_lcd_need_redraw = true;
        goto_page(PAGE_SENSOR_SETTINGS);
    }
    else if (cur == PAGE_TEMP_MODE) {
        g_temp_mode = (temp_mode_t)sel;
        Save_Temp_Settings_To_Storage();
        ESP_LOGI(TAG_MENU, "Luu che do nhiet do NVS: %d", g_temp_mode);
        g_lcd_need_redraw = true;
        goto_page(PAGE_SENSOR_SETTINGS);
    }
    else if (cur == PAGE_CAL_2PT) {
        PH_Temp_Sensor_Status_t status = Get_Sensor_Status();
        bool ok = false;
        if (sel == 0) {
            ok = Calibrate_PH_Point(4.00f, status.v_probe_mv, status.temperature, 2);
        } else if (sel == 1) {
            ok = Calibrate_PH_Point(7.00f, status.v_probe_mv, status.temperature, 2);
        }
        ESP_LOGI(TAG_MENU, "Hieu chuan pH 2 diem: %d -> %s", sel, ok ? "OK" : "ERR");
        goto_page(PAGE_CALIBRATION);
        g_lcd_need_redraw = true;
    }
    else if (cur == PAGE_CAL_3PT_G1) {
        PH_Temp_Sensor_Status_t status = Get_Sensor_Status();
        bool ok = false;
        if (sel == 0) {
            ok = Calibrate_PH_Point(4.00f, status.v_probe_mv, status.temperature, 3);
        } else if (sel == 1) {
            ok = Calibrate_PH_Point(6.86f, status.v_probe_mv, status.temperature, 3);
        } else if (sel == 2) {
            ok = Calibrate_PH_Point(9.18f, status.v_probe_mv, status.temperature, 3);
        }
        ESP_LOGI(TAG_MENU, "Hieu chuan pH 3 diem N1: %d -> %s", sel, ok ? "OK" : "ERR");
        goto_page(PAGE_CAL_3PT);
        g_lcd_need_redraw = true;
    }
    else if (cur == PAGE_CAL_3PT_G2) {
        PH_Temp_Sensor_Status_t status = Get_Sensor_Status();
        bool ok = false;
        if (sel == 0) {
            ok = Calibrate_PH_Point(4.00f, status.v_probe_mv, status.temperature, 3);
        } else if (sel == 1) {
            ok = Calibrate_PH_Point(7.00f, status.v_probe_mv, status.temperature, 3);
        } else if (sel == 2) {
            ok = Calibrate_PH_Point(10.00f, status.v_probe_mv, status.temperature, 3);
        }
        ESP_LOGI(TAG_MENU, "Hieu chuan pH 3 diem N2: %d -> %s", sel, ok ? "OK" : "ERR");
        goto_page(PAGE_CAL_3PT);
        g_lcd_need_redraw = true;
    }
    else if (cur == PAGE_CAL_DO) {
        if (sel == 0) {
            do_sensor_calibrate_zero();
            goto_page(PAGE_CALIBRATION);
        } else if (sel == 1) {
            do_sensor_calibrate_slope();
            goto_page(PAGE_CALIBRATION);
        } else if (sel == 2) {
            do_sensor_correct_temp(25.0f);
            goto_page(PAGE_CALIBRATION);
        } else if (sel == 3) {
            do_sensor_reset();
            goto_page(PAGE_CALIBRATION);
        }
        g_lcd_need_redraw = true;
    }
    else if (cur == PAGE_MODBUS_SELECT_BAUD) {
        uint32_t bauds[] = {2400, 4800, 9600, 19200, 38400, 57600, 115200};
        uint32_t val = bauds[sel];
        g_mb2_baud = val;
        Nvs_Write_Number("mb2_baud", val);
        do_sensor_update_config(g_mb2_addr, g_mb2_baud, g_mb2_parity, g_mb2_stop);
        ESP_LOGI(TAG_MENU, "Luu Baudrate Port 2: %lu", (unsigned long)val);
        goto_page(PAGE_MODBUS_PORT2);
        g_lcd_need_redraw = true;
    }
    else if (cur == PAGE_MODBUS_SELECT_PARITY) {
        g_mb2_parity = sel;
        Nvs_Write_Number("mb2_parity", sel);
        do_sensor_update_config(g_mb2_addr, g_mb2_baud, g_mb2_parity, g_mb2_stop);
        ESP_LOGI(TAG_MENU, "Luu Parity Port 2: %d", sel);
        goto_page(PAGE_MODBUS_PORT2);
        g_lcd_need_redraw = true;
    }
    else if (cur == PAGE_MODBUS_SELECT_STOP) {
        uint8_t stops[] = {1, 2};
        g_mb2_stop = stops[sel];
        Nvs_Write_Number("mb2_stop", g_mb2_stop);
        do_sensor_update_config(g_mb2_addr, g_mb2_baud, g_mb2_parity, g_mb2_stop);
        ESP_LOGI(TAG_MENU, "Luu Stop Bits Port 2: %d", g_mb2_stop);
        goto_page(PAGE_MODBUS_PORT2);
        g_lcd_need_redraw = true;
    }
}

void menu_init(void)
{
    ESP_LOGI(TAG_MENU, "Khoi tao GPIO nut bam voi PULL-UP noi bo...");

    /* Tất cả các chân nút nhấn (1, 2, 3, 4, 5) đều hỗ trợ pull-up nội trên ESP32-S3 */
    gpio_config_t cfg_pullup = {
        .pin_bit_mask = (1ULL << BTN_PIN_ESC)
                      | (1ULL << BTN_PIN_DOWN)
                      | (1ULL << BTN_PIN_UP)
                      | (1ULL << BTN_PIN_RIGHT)
                      | (1ULL << BTN_PIN_ENTER),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg_pullup);

    ESP_LOGI(TAG_MENU, "GPIO nut bam (ho tro pull-up) san sang.");
}

/* =====================================================================
 * Xử lý nút bấm & cập nhật trạng thái menu
 * ===================================================================== */
void menu_handle_buttons(void)
{
    /* Đọc và chống rung toàn bộ nút bấm */
    poll_and_debounce_buttons();

    if (g_menu.in_pin_entry) {
        menu_pin_handle_buttons();
        return;
    }

    /* ── Màn hình đo lường ── */
    if (!g_menu.in_menu) {
        /* ENTER: vao menu */
        if (btn_edge(BTN_IDX_ENTER)) {
            g_menu.in_menu = true;
            goto_page(PAGE_MAIN_MENU);
            g_lcd_need_redraw = true;
        }
        /* RIGHT (nut ngang): bat/tat bieu do toan man hinh */
        if (btn_edge(BTN_IDX_RIGHT)) {
            g_display_view = (g_display_view == DISP_VIEW_CHART)
                                 ? DISP_VIEW_NUMBER
                                 : DISP_VIEW_CHART;
            Nvs_Write_Number("disp_view", (uint32_t)g_display_view);
            g_lcd_need_redraw = true;
            ESP_LOGI(TAG_MENU, "Chuyen kieu hien thi: %d", g_display_view);
        }
        return;
    }

    /* ── Trong menu ── */
    const page_def_t *page = (g_sys_lang == LANG_VI) ? &s_pages_vi[g_menu.current_page] : &s_pages_en[g_menu.current_page];

    if (page->title == NULL) {
        /* Nếu trang chưa khởi tạo, chỉ cho phép ESC quay lại Main Menu để tránh crash */
        if (btn_edge(BTN_IDX_ESC)) {
            goto_page(PAGE_MAIN_MENU);
            g_lcd_need_redraw = true;
        }
        return;
    }

    /* Xử lý phím riêng biệt cho màn hình cài đặt Contrast */
    if (g_menu.current_page == PAGE_SCREEN_CONTRAST) {
        if (btn_edge(BTN_IDX_ESC)) {
            goto_page(page->parent);
            g_lcd_need_redraw = true;
        }
        if (btn_edge(BTN_IDX_UP)) {
            if (g_lcd_contrast < 63) {
                g_lcd_contrast++;
                LCD_SetContrast(g_lcd_contrast);
                Nvs_Write_Number("lcd_contrast", g_lcd_contrast);
                g_lcd_need_redraw = true;
                ESP_LOGI(TAG_MENU, "Contrast tang: %d", g_lcd_contrast);
            }
        }
        if (btn_edge(BTN_IDX_DOWN)) {
            if (g_lcd_contrast > 0) {
                g_lcd_contrast--;
                LCD_SetContrast(g_lcd_contrast);
                Nvs_Write_Number("lcd_contrast", g_lcd_contrast);
                g_lcd_need_redraw = true;
                ESP_LOGI(TAG_MENU, "Contrast giam: %d", g_lcd_contrast);
            }
        }
        return;
    }

    /* Xử lý phím riêng biệt cho màn hình cài đặt Resistor Ratio */
    if (g_menu.current_page == PAGE_SCREEN_RES_RATIO) {
        if (btn_edge(BTN_IDX_ESC)) {
            goto_page(page->parent);
            g_lcd_need_redraw = true;
        }
        if (btn_edge(BTN_IDX_UP)) {
            if (g_lcd_resistor_ratio < 7) {
                g_lcd_resistor_ratio++;
                LCD_SetResistorRatio(g_lcd_resistor_ratio);
                Nvs_Write_Number("lcd_res_ratio", g_lcd_resistor_ratio);
                g_lcd_need_redraw = true;
                ESP_LOGI(TAG_MENU, "Resistor Ratio tang: %d", g_lcd_resistor_ratio);
            }
        }
        if (btn_edge(BTN_IDX_DOWN)) {
            if (g_lcd_resistor_ratio > 0) {
                g_lcd_resistor_ratio--;
                LCD_SetResistorRatio(g_lcd_resistor_ratio);
                Nvs_Write_Number("lcd_res_ratio", g_lcd_resistor_ratio);
                g_lcd_need_redraw = true;
                ESP_LOGI(TAG_MENU, "Resistor Ratio giam: %d", g_lcd_resistor_ratio);
            }
        }
        return;
    }

    /* Xử lý phím riêng cho màn hình Cài đặt giờ thủ công */
    if (g_menu.current_page == PAGE_TIME_SETTINGS) {
        if (btn_edge(BTN_IDX_ESC)) {
            goto_page(page->parent);
            g_lcd_need_redraw = true;
            return;
        }
        if (btn_edge(BTN_IDX_UP)) {
            time_edit_adjust(+1);
            s_time_saved_msg = false;
            g_lcd_need_redraw = true;
        }
        if (btn_edge(BTN_IDX_DOWN)) {
            time_edit_adjust(-1);
            s_time_saved_msg = false;
            g_lcd_need_redraw = true;
        }
        if (btn_edge(BTN_IDX_RIGHT)) {
            s_time_field = (uint8_t)((s_time_field + 1) % TIME_FIELD_COUNT);
            s_time_saved_msg = false;
            g_lcd_need_redraw = true;
        }
        if (btn_edge(BTN_IDX_ENTER)) {
            time_edit_save();
            g_lcd_need_redraw = true;
        }
        return;
    }

    /* ESC – quay lại trang cha */
    if (btn_edge(BTN_IDX_ESC)) {
        menu_page_t parent = page->parent;
        if (parent == PAGE_MEASUREMENT) {
            g_menu.in_menu      = false;
            g_menu.current_page = PAGE_MEASUREMENT;
            g_lcd_need_redraw   = true;
            ESP_LOGI(TAG_MENU, "Thoat menu -> do luong");
        } else {
            goto_page(parent);
            g_lcd_need_redraw = true;
        }
    }

    /* UP – di chuyển lên */
    if (btn_edge(BTN_IDX_UP)) {
        if (g_menu.selected > 0) {
            g_menu.selected--;
            if (g_menu.selected < g_menu.scroll_offset) {
                g_menu.scroll_offset = g_menu.selected;
            }
            g_lcd_need_redraw = true;
        }
    }

    /* DOWN – di chuyển xuống */
    if (btn_edge(BTN_IDX_DOWN)) {
        if (g_menu.selected < page->item_count - 1) {
            g_menu.selected++;
            if (g_menu.selected >= g_menu.scroll_offset + VISIBLE_ITEMS) {
                g_menu.scroll_offset = g_menu.selected - VISIBLE_ITEMS + 1;
            }
            g_lcd_need_redraw = true;
        }
    }

    /* ENTER – vào mục con */
    if (btn_edge(BTN_IDX_ENTER)) {
        menu_page_t child = page->children[g_menu.selected];
        if (g_menu.current_page == PAGE_MAIN_MENU &&
            (child == PAGE_MODBUS_SETTINGS || child == PAGE_SENSOR_SETTINGS)) {
            menu_pin_begin_entry(child);
            g_lcd_need_redraw = true;
        } else if (child != PAGE_LEAF && child != PAGE_COUNT) {
            goto_page(child);
            g_lcd_need_redraw = true;
        } else {
            /* Mục lá: Thực thi tác vụ khi chọn mục này */
            menu_handle_leaf_select();
        }
    }

    /* RIGHT – dự phòng (bỏ qua) */
    btn_edge(BTN_IDX_RIGHT);
}

/* =====================================================================
 * Vẽ màn hình menu lên LCD
 * ===================================================================== */

/** Vẽ mũi tên đơn giản bằng pixel (▲ hoặc ▼) tại (x,y) */
static void draw_arrow_up(uint8_t x, uint8_t y, uint8_t color)
{
    /* 5×4 px arrow ▲ */
    LCD_DrawPixel(x + 2, y,     color);
    LCD_DrawPixel(x + 1, y + 1, color);
    LCD_DrawPixel(x + 2, y + 1, color);
    LCD_DrawPixel(x + 3, y + 1, color);
    LCD_DrawHLine(x,     y + 2, 5, color);
    LCD_DrawHLine(x,     y + 3, 5, color);
}

static void draw_arrow_down(uint8_t x, uint8_t y, uint8_t color)
{
    /* 5×4 px arrow ▼ */
    LCD_DrawHLine(x,     y,     5, color);
    LCD_DrawHLine(x,     y + 1, 5, color);
    LCD_DrawPixel(x + 1, y + 2, color);
    LCD_DrawPixel(x + 2, y + 2, color);
    LCD_DrawPixel(x + 3, y + 2, color);
    LCD_DrawPixel(x + 2, y + 3, color);
}

static void draw_arrow_right(uint8_t x, uint8_t y, uint8_t color)
{
    /* 3×5 px arrow ► chỉ mục con */
    LCD_DrawVLine(x,     y,     5, color);
    LCD_DrawVLine(x + 1, y + 1, 3, color);
    LCD_DrawPixel(x + 2, y + 2, color);
}

/** Vẽ các mũi tên đặc lớn (7×7 px) cho thanh công cụ phía dưới */
static void draw_arrow_up_large(uint8_t x, uint8_t y, uint8_t color)
{
    /* 7×7 px solid arrow ▲ */
    LCD_DrawPixel(x + 3, y,     color);
    LCD_DrawHLine(x + 2, y + 1, 3, color);
    LCD_DrawHLine(x + 2, y + 2, 3, color);
    LCD_DrawHLine(x + 1, y + 3, 5, color);
    LCD_DrawHLine(x + 1, y + 4, 5, color);
    LCD_DrawHLine(x,     y + 5, 7, color);
    LCD_DrawHLine(x,     y + 6, 7, color);
}

static void draw_arrow_down_large(uint8_t x, uint8_t y, uint8_t color)
{
    /* 7×7 px solid arrow ▼ */
    LCD_DrawHLine(x,     y,     7, color);
    LCD_DrawHLine(x,     y + 1, 7, color);
    LCD_DrawHLine(x + 1, y + 2, 5, color);
    LCD_DrawHLine(x + 1, y + 3, 5, color);
    LCD_DrawHLine(x + 2, y + 4, 3, color);
    LCD_DrawHLine(x + 2, y + 5, 3, color);
    LCD_DrawPixel(x + 3, y + 6, color);
}

static void draw_arrow_right_large(uint8_t x, uint8_t y, uint8_t color)
{
    /* 7×7 px solid arrow ► */
    LCD_DrawVLine(x,     y,     7, color);
    LCD_DrawVLine(x + 1, y,     7, color);
    LCD_DrawVLine(x + 2, y + 1, 5, color);
    LCD_DrawVLine(x + 3, y + 1, 5, color);
    LCD_DrawVLine(x + 4, y + 2, 3, color);
    LCD_DrawVLine(x + 5, y + 2, 3, color);
    LCD_DrawPixel(x + 6, y + 3, color);
}

static void menu_pin_render(void)
{
    LCD_Clear();

    LCD_FillRect(0, 0, 128, TITLE_BAR_H, LCD_COLOR_ON);
    if (s_pin_target_page == PAGE_SENSOR_SETTINGS) {
        if (g_sys_lang == LANG_VI) {
            LCD_DrawString(4, 2, "Mat Khau Cam Bien", LCD_COLOR_OFF);
        } else {
            LCD_DrawString(4, 2, "Sensor Password", LCD_COLOR_OFF);
        }
    } else {
        if (g_sys_lang == LANG_VI) {
            LCD_DrawString(4, 2, "Mat Khau Modbus", LCD_COLOR_OFF);
        } else {
            LCD_DrawString(4, 2, "Modbus Password", LCD_COLOR_OFF);
        }
    }

    /* Con dem hien so: het thoi gian thi an thanh '*' */
    bool reveal_on = false;
    if (s_pin_reveal > 0) {
        s_pin_reveal--;
        reveal_on = true;
        if (s_pin_reveal > 0) {
            g_lcd_need_redraw = true;  /* tiep tuc ve de dem nguoc */
        }
    }

    const uint8_t digit_x0 = 22;
    const uint8_t digit_step = 21;
    const uint8_t digit_y = 24;

    for (uint8_t i = 0; i < MENU_PIN_LEN; i++) {
        uint8_t x = digit_x0 + i * digit_step;
        bool focused = (i == s_pin_cursor);
        uint8_t color = focused ? LCD_COLOR_OFF : LCD_COLOR_ON;

        if (focused) {
            LCD_FillRect(x - 2, digit_y - 1, 14, 17, LCD_COLOR_ON);
        }

        if (focused && reveal_on) {
            char digit = (char)('0' + s_pin_entry[i]);
            LCD_DrawMediumChar(x, digit_y, digit, color);
        } else {
            LCD_DrawChar(x + 4, digit_y + 4, '*', color);
        }
    }

    if (s_pin_show_error) {
        if (g_sys_lang == LANG_VI) {
            LCD_DrawString(24, 42, "Sai mat khau!", LCD_COLOR_ON);
        } else {
            LCD_DrawString(28, 42, "Wrong PIN!", LCD_COLOR_ON);
        }
    } else if (g_sys_lang == LANG_VI) {
        LCD_DrawString(4, 42, "UP/DN: doi  RIGHT: next", LCD_COLOR_ON);
    } else {
        LCD_DrawString(4, 42, "UP/DN: chg  RIGHT: next", LCD_COLOR_ON);
    }

    LCD_FillRect(0, STATUS_BAR_Y, 128, STATUS_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(8, STATUS_BAR_Y + 3, "ESC", LCD_COLOR_OFF);
    draw_arrow_down_large(39, STATUS_BAR_Y + 3, LCD_COLOR_OFF);
    draw_arrow_up_large(60, STATUS_BAR_Y + 3, LCD_COLOR_OFF);
    draw_arrow_right_large(81, STATUS_BAR_Y + 3, LCD_COLOR_OFF);
    LCD_DrawString(102, STATUS_BAR_Y + 3, "ENT", LCD_COLOR_OFF);
    LCD_Flush();
}

static void menu_pin_handle_buttons(void)
{
    if (btn_edge(BTN_IDX_ESC)) {
        g_menu.in_pin_entry = false;
        s_pin_show_error = false;
        g_lcd_need_redraw = true;
        ESP_LOGI(TAG_MENU, "Huy nhap mat khau");
        return;
    }

    if (btn_edge(BTN_IDX_UP)) {
        s_pin_entry[s_pin_cursor] = (uint8_t)((s_pin_entry[s_pin_cursor] + 1) % 10);
        s_pin_show_error = false;
        s_pin_reveal = MENU_PIN_REVEAL_TICKS;
        g_lcd_need_redraw = true;
    }

    if (btn_edge(BTN_IDX_DOWN)) {
        s_pin_entry[s_pin_cursor] =
            (uint8_t)((s_pin_entry[s_pin_cursor] + 9) % 10);
        s_pin_show_error = false;
        s_pin_reveal = MENU_PIN_REVEAL_TICKS;
        g_lcd_need_redraw = true;
    }

    if (btn_edge(BTN_IDX_RIGHT)) {
        s_pin_cursor = (uint8_t)((s_pin_cursor + 1) % MENU_PIN_LEN);
        s_pin_show_error = false;
        s_pin_reveal = 0;   /* an ngay so o cu khi chuyen sang o moi */
        g_lcd_need_redraw = true;
    }

    if (btn_edge(BTN_IDX_ENTER)) {
        if (menu_pin_verify()) {
            g_menu.in_pin_entry = false;
            s_pin_show_error = false;
            goto_page(s_pin_target_page);
            g_lcd_need_redraw = true;
            ESP_LOGI(TAG_MENU, "Mat khau dung -> vao menu %d", (int)s_pin_target_page);
        } else {
            s_pin_show_error = true;
            g_lcd_need_redraw = true;
            ESP_LOGW(TAG_MENU, "Mat khau sai");
        }
    }
}

void menu_render(void)
{
    if (g_menu.in_pin_entry) {
        menu_pin_render();
        return;
    }

    const page_def_t *page = (g_sys_lang == LANG_VI) ? &s_pages_vi[g_menu.current_page] : &s_pages_en[g_menu.current_page];
    if (page == NULL || page->title == NULL) {
        ESP_LOGE(TAG_MENU, "Page %d not initialized!", g_menu.current_page);
        LCD_Clear();
        LCD_DrawString(10, 20, "Page Error!", LCD_COLOR_ON);
        LCD_DrawString(10, 32, "Press ESC to go back", LCD_COLOR_ON);
        LCD_FillRect(0, STATUS_BAR_Y, 128, STATUS_BAR_H, LCD_COLOR_ON);
        LCD_DrawString(8, STATUS_BAR_Y + 3, "ESC", LCD_COLOR_OFF);
        LCD_Flush();
        return;
    }
    LCD_Clear();

    /* ── 1. Title bar (y=0..11): nền đen, chữ trắng ── */
    LCD_FillRect(0, 0, 128, TITLE_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(4, 2, page->title, LCD_COLOR_OFF);

    if (g_menu.current_page == PAGE_SCREEN_CONTRAST) {
        /* ── 2. Giao diện điều chỉnh độ tương phản (y=13..50) ── */
        LCD_DrawString(16, 16, "Adjust Contrast", LCD_COLOR_ON);

        // Vẽ khung thanh trượt (slider border)
        // Chiều rộng 100 pixel (từ cột 14 đến cột 113)
        LCD_DrawRect(14, 28, 100, 8, LCD_COLOR_ON);

        // Tính toán chiều rộng thanh hiển thị (bar width)
        // Độ tương phản từ 0 đến 63. Mép trong thanh trượt có chiều rộng là 96 pixel (16..111)
        uint8_t bar_w = (g_lcd_contrast * 96) / 63;
        if (bar_w > 96) bar_w = 96;
        LCD_FillRect(16, 30, bar_w, 4, LCD_COLOR_ON);

        // Hiển thị số đọc
        char val_str[16];
        snprintf(val_str, sizeof(val_str), "Value: %d", g_lcd_contrast);
        LCD_DrawString(44, 40, val_str, LCD_COLOR_ON);
    } else if (g_menu.current_page == PAGE_SCREEN_RES_RATIO) {
        /* ── 2. Giao diện điều chỉnh tỷ số điện trở (y=13..50) ── */
        LCD_DrawString(16, 16, (g_sys_lang == LANG_VI) ? "Cai Ty So Dien Tro" : "Adjust Resistor Ratio", LCD_COLOR_ON);

        // Vẽ khung thanh trượt (slider border)
        LCD_DrawRect(14, 28, 100, 8, LCD_COLOR_ON);

        // Tính toán chiều rộng thanh hiển thị (Tỷ số điện trở từ 0 đến 7)
        uint8_t bar_w = (g_lcd_resistor_ratio * 96) / 7;
        if (bar_w > 96) bar_w = 96;
        LCD_FillRect(16, 30, bar_w, 4, LCD_COLOR_ON);

        char val_str[16];
        snprintf(val_str, sizeof(val_str), "Value: %d", g_lcd_resistor_ratio);
        LCD_DrawString(44, 40, val_str, LCD_COLOR_ON);
    } else if (g_menu.current_page == PAGE_TIME_SETTINGS) {
        /* ── 2. Cài đặt giờ thủ công (YYYY-MM-DD / HH:MM:SS) ── */
        char datebuf[40];
        char timebuf[40];
        snprintf(datebuf, sizeof(datebuf), "%04d-%02d-%02d",
                 s_time_edit.tm_year + 1900, s_time_edit.tm_mon + 1,
                 s_time_edit.tm_mday);
        snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d", s_time_edit.tm_hour,
                 s_time_edit.tm_min, s_time_edit.tm_sec);

        const uint8_t date_x = 34, date_y = 18;
        const uint8_t time_x = 40, time_y = 32;
        LCD_DrawString(date_x, date_y, datebuf, LCD_COLOR_ON);
        LCD_DrawString(time_x, time_y, timebuf, LCD_COLOR_ON);

        /* Ô đang chọn: nền đen chữ trắng */
        uint8_t fx = date_x, fy = date_y, foff = 0, flen = 4;
        const char *fs = datebuf;
        switch (s_time_field) {
        case 0: fx = date_x;            foff = 0; flen = 4; fs = datebuf; fy = date_y; break;
        case 1: fx = date_x + 5 * 6;    foff = 5; flen = 2; fs = datebuf; fy = date_y; break;
        case 2: fx = date_x + 8 * 6;    foff = 8; flen = 2; fs = datebuf; fy = date_y; break;
        case 3: fx = time_x;            foff = 0; flen = 2; fs = timebuf; fy = time_y; break;
        case 4: fx = time_x + 3 * 6;    foff = 3; flen = 2; fs = timebuf; fy = time_y; break;
        case 5: fx = time_x + 6 * 6;    foff = 6; flen = 2; fs = timebuf; fy = time_y; break;
        default: break;
        }
        LCD_FillRect((uint8_t)(fx - 1), (uint8_t)(fy - 1),
                     (uint8_t)(flen * 6 + 1), 9, LCD_COLOR_ON);
        char sub[6];
        memcpy(sub, fs + foff, flen);
        sub[flen] = '\0';
        LCD_DrawString(fx, fy, sub, LCD_COLOR_OFF);

        if (s_time_saved_msg) {
            LCD_DrawString(40, (uint8_t)(time_y + 14),
                           (g_sys_lang == LANG_VI) ? "Da luu!" : "Saved!",
                           LCD_COLOR_ON);
        } else {
            LCD_DrawString(4, (uint8_t)(time_y + 14),
                           (g_sys_lang == LANG_VI) ? "ENT:luu RIGHT:doi o"
                                                   : "ENT:save RIGHT:field",
                           LCD_COLOR_ON);
        }
    } else {
        /* ── 2. Danh sách mục (y=13..48) ── */
        uint8_t visible = page->item_count - g_menu.scroll_offset;
        if (visible > VISIBLE_ITEMS) visible = VISIBLE_ITEMS;

        for (uint8_t i = 0; i < visible; i++) {
            uint8_t idx = g_menu.scroll_offset + i;
            uint8_t y   = ITEMS_Y_START + i * ITEM_ROW_H;

            bool is_active_choice = false;
            if (g_menu.current_page == PAGE_LANGUAGE && idx == (uint8_t)g_sys_lang) {
                is_active_choice = true;
            }
            else if (g_menu.current_page == PAGE_DATE_FORMAT && idx == (uint8_t)g_date_format) {
                is_active_choice = true;
            }
            else if (g_menu.current_page == PAGE_DISPLAY_MODE) {
                if (idx == (uint8_t)g_display_mode) {
                    is_active_choice = true;
                }
            }
            else if (g_menu.current_page == PAGE_DIGITAL_FILTER && idx == (uint8_t)g_filter_level) {
                is_active_choice = true;
            }
            else if (g_menu.current_page == PAGE_TEMP_MODE && idx == (uint8_t)g_temp_mode) {
                is_active_choice = true;
            }
            else if (g_menu.current_page == PAGE_MODBUS_SELECT_BAUD) {
                uint32_t bauds[] = {2400, 4800, 9600, 19200, 38400, 57600, 115200};
                if (bauds[idx] == g_mb2_baud) {
                    is_active_choice = true;
                }
            }
            else if (g_menu.current_page == PAGE_MODBUS_SELECT_PARITY && idx == g_mb2_parity) {
                is_active_choice = true;
            }
            else if (g_menu.current_page == PAGE_MODBUS_SELECT_STOP) {
                uint8_t stops[] = {1, 2};
                if (stops[idx] == g_mb2_stop) {
                    is_active_choice = true;
                }
            }

            if (idx == g_menu.selected) {
                /* Mục được chọn: nền đen, chữ trắng */
                LCD_FillRect(0, y, 128, ITEM_ROW_H, LCD_COLOR_ON);
                LCD_DrawString(4, y + 1, page->items[idx], LCD_COLOR_OFF);
                /* Mũi tên ► ở bên phải để chỉ mục đang chọn */
                if (page->children[idx] != PAGE_LEAF) {
                    draw_arrow_right(121, y + 2, LCD_COLOR_OFF);
                }
                if (is_active_choice) {
                    LCD_DrawChar(112, y + 1, '*', LCD_COLOR_OFF);
                }
            } else {
                /* Mục bình thường: nền trắng, chữ đen */
                LCD_DrawString(4, y + 1, page->items[idx], LCD_COLOR_ON);
                if (is_active_choice) {
                    LCD_DrawChar(112, y + 1, '*', LCD_COLOR_ON);
                }
            }
        }

        /* ── 3. Scroll indicator (mũi tên nhỏ ngoài rìa phải) ── */
        if (g_menu.scroll_offset > 0) {
            /* Còn mục phía trên */
            draw_arrow_up(120, ITEMS_Y_START + 1, LCD_COLOR_ON);
        }
        if (g_menu.scroll_offset + VISIBLE_ITEMS < page->item_count) {
            /* Còn mục phía dưới */
            draw_arrow_down(120, STATUS_BAR_Y - 6, LCD_COLOR_ON);
        }
    }

    /* ── 4. Status bar (y=51..63): nền đen, nhãn nút ── */
    LCD_FillRect(0, STATUS_BAR_Y, 128, STATUS_BAR_H, LCD_COLOR_ON);

    /* "ESC" */
    LCD_DrawString(8, STATUS_BAR_Y + 3, "ESC", LCD_COLOR_OFF);

    /* Mũi tên ▼ (DOWN) */
    draw_arrow_down_large(39, STATUS_BAR_Y + 3, LCD_COLOR_OFF);

    /* Mũi tên ▲ (UP) */
    draw_arrow_up_large(60, STATUS_BAR_Y + 3, LCD_COLOR_OFF);

    /* Mũi tên ► (RIGHT) */
    draw_arrow_right_large(81, STATUS_BAR_Y + 3, LCD_COLOR_OFF);

    /* "ENT" */
    LCD_DrawString(102, STATUS_BAR_Y + 3, "ENT", LCD_COLOR_OFF);

    LCD_Flush();
}

bool menu_simulate_press(const char *btn_name)
{
    if (btn_name == NULL) return false;
    btn_idx_t idx = BTN_COUNT;
    if (strcmp(btn_name, "esc") == 0) {
        idx = BTN_IDX_ESC;
    } else if (strcmp(btn_name, "down") == 0) {
        idx = BTN_IDX_DOWN;
    } else if (strcmp(btn_name, "up") == 0) {
        idx = BTN_IDX_UP;
    } else if (strcmp(btn_name, "right") == 0) {
        idx = BTN_IDX_RIGHT;
    } else if (strcmp(btn_name, "enter") == 0) {
        idx = BTN_IDX_ENTER;
    }
    
    if (idx < BTN_COUNT) {
        s_btn_simulated[idx] = true;
        return true;
    }
    return false;
}
