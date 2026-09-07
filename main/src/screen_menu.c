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
#include "user_fram.h"
#include "wifi_config_manager.h"
#include "esp_netif.h"
#include "user_system.h"



#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "user_azure.h"
#include "user_ota.h"
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
        .item_count = 7,
        .items      = { "1 System Settings",
                        "2 Display Settings",
                        "3 Modbus Settings",
                        "4 Sensor Settings",
                        "5 History Log",
                        "6 WiFi Settings",
                        "7 Device Info" },
        .children   = { PAGE_SYSTEM_SETTINGS,
                        PAGE_DISPLAY_MODE,
                        PAGE_MODBUS_SETTINGS,
                        PAGE_SENSOR_SETTINGS,
                        PAGE_HISTORY_LOG,
                        PAGE_WIFI_SETTINGS,
                        PAGE_DEVICE_INFO },
        .parent     = PAGE_MEASUREMENT,
    },

    [PAGE_DEVICE_INFO] = {
        .title      = "Device Info",
        .item_count = 0,
        .parent     = PAGE_MAIN_MENU,
    },

    [PAGE_HISTORY_LOG] = {
        .title      = "History Log",
        .item_count = 0,
        .parent     = PAGE_MAIN_MENU,
    },

    [PAGE_WIFI_SETTINGS] = {
        .title      = "WiFi Settings",
        .item_count = 2,
        .items      = { "6.1 Scan & Connect",
                        "6.2 WiFi Status" },
        .children   = { PAGE_WIFI_SCAN_LIST, PAGE_WIFI_STATUS },
        .parent     = PAGE_MAIN_MENU,
    },

    [PAGE_WIFI_SCAN_LIST] = {
        .title      = "WiFi Networks",
        .item_count = 0,
        .parent     = PAGE_WIFI_SETTINGS,
    },

    [PAGE_WIFI_PASS_ENTRY] = {
        .title      = "Enter Password",
        .item_count = 0,
        .parent     = PAGE_WIFI_SCAN_LIST,
    },

    [PAGE_WIFI_STATUS] = {
        .title      = "WiFi Status",
        .item_count = 0,
        .parent     = PAGE_WIFI_SETTINGS,
    },


    /* ── System Settings ── */
    [PAGE_SYSTEM_SETTINGS] = {
        .title      = "System Settings",
        .item_count = 4,
        .items      = { "1.1 Language",
                        "1.2 Date",
                        "1.3 Screen Settings",
                        "1.4 Change Password" },
        .children   = { PAGE_LANGUAGE, PAGE_DATE, PAGE_SCREEN_SETTINGS, PAGE_CHANGE_PIN },
        .parent     = PAGE_MAIN_MENU,
    },

    [PAGE_CHANGE_PIN] = {
        .title      = "Change Password",
        .item_count = 0,
        .parent     = PAGE_SYSTEM_SETTINGS,
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
        .item_count = 2,
        .items      = { "4.1 pH Sensor Settings",
                        "4.2 DO Sensor Settings" },
        .children   = { PAGE_PH_SETTINGS,
                        PAGE_DO_SETTINGS },
        .parent     = PAGE_MAIN_MENU,
    },

    [PAGE_PH_SETTINGS] = {
        .title      = "pH Sensor Settings",
        .item_count = 6,
        .items      = { "4.1.1 Calibration",
                        "4.1.2 Sensor Health",
                        "4.1.3 Digital Filter",
                        "4.1.4 Temp Mode",
                        "4.1.5 Temp Settings",
                        "4.1.6 Linear Comp" },
        .children   = { PAGE_CALIBRATION,
                        PAGE_PH_HEALTH,
                        PAGE_DIGITAL_FILTER,
                        PAGE_TEMP_MODE,
                        PAGE_TEMP_SETTINGS,
                        PAGE_TEMP_LIN_COMP },
        .parent     = PAGE_SENSOR_SETTINGS,
    },

    [PAGE_PH_HEALTH] = {
        .title      = "pH Sensor Health",
        .item_count = 0,
        .parent     = PAGE_PH_SETTINGS,
    },

    [PAGE_DO_SETTINGS] = {
        .title      = "DO Sensor Settings",
        .item_count = 2,
        .items      = { "4.2.1 DO Calibration",
                        "4.2.2 Reset DO Cal" },
        .children   = { PAGE_CAL_DO, PAGE_LEAF },
        .parent     = PAGE_SENSOR_SETTINGS,
    },

    [PAGE_CALIBRATION] = {
        .title      = "pH Calibration",
        .item_count = 3,
        .items      = { "4.1.1.1 Cal. 2 point",
                        "4.1.1.2 Cal. 3 point",
                        "4.1.1.3 Reset pH Cal" },
        .children   = { PAGE_CAL_2PT, PAGE_CAL_3PT, PAGE_LEAF },
        .parent     = PAGE_PH_SETTINGS,
    },

    [PAGE_CAL_DO] = {
        .title      = "DO Calibration",
        .item_count = 3,
        .items      = { "1. Cal. DO Zero",
                        "2. Cal. DO Slope",
                        "3. Cal. DO Temp" },
        .children   = { PAGE_CAL_DO_EXEC, PAGE_CAL_DO_EXEC, PAGE_CAL_DO_TEMP },
        .parent     = PAGE_DO_SETTINGS,
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
        .items      = { "4.1.2.1 L",
                        "4.1.2.2 M",
                        "4.1.2.3 H" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_PH_SETTINGS,
    },

    [PAGE_TEMP_MODE] = {
        .title      = "Temp Mode",
        .item_count = 4,
        .items      = { "4.1.3.1 ATC  C",
                        "4.1.3.2 MTC  C",
                        "4.1.3.3 ATF  F",
                        "4.1.3.4 MTF  F" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_PH_SETTINGS,
    },

    [PAGE_TEMP_SETTINGS] = {
        .title      = "Temp Settings",
        .item_count = 0,
        .parent     = PAGE_PH_SETTINGS,
    },

    [PAGE_TEMP_LIN_COMP] = {
        .title      = "Temp Lin COMP",
        .item_count = 0,
        .parent     = PAGE_PH_SETTINGS,
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

    [PAGE_RESET_SENSOR] = {
        .title      = "Reset Sensor",
        .item_count = 2,
        .items      = { "1. Reset pH",
                        "2. Reset DO" },
        .children   = { PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_CALIBRATION,
    },

    [PAGE_CONFIRM_RESET] = {
        .title      = "Confirm Reset",
        .item_count = 0,
        .parent     = PAGE_CALIBRATION,
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
        .item_count = 7,
        .items      = { "1 Cai Dat He Thong",
                        "2 Cai Dat Hien Thi",
                        "3 Cai Dat Modbus",
                        "4 Cai Dat Cam Bien",
                        "5 Xem Lai Lich Su",
                        "6 Cai Dat WiFi",
                        "7 Thong Tin Thiet Bi" },
        .children   = { PAGE_SYSTEM_SETTINGS,
                        PAGE_DISPLAY_MODE,
                        PAGE_MODBUS_SETTINGS,
                        PAGE_SENSOR_SETTINGS,
                        PAGE_HISTORY_LOG,
                        PAGE_WIFI_SETTINGS,
                        PAGE_DEVICE_INFO },
        .parent     = PAGE_MEASUREMENT,
    },

    [PAGE_DEVICE_INFO] = {
        .title      = "Thong Tin Thiet Bi",
        .item_count = 0,
        .parent     = PAGE_MAIN_MENU,
    },

    [PAGE_HISTORY_LOG] = {
        .title      = "Xem Lai Lich Su",
        .item_count = 0,
        .parent     = PAGE_MAIN_MENU,
    },

    [PAGE_WIFI_SETTINGS] = {
        .title      = "Cai Dat WiFi",
        .item_count = 2,
        .items      = { "6.1 Quet & Ket Noi",
                        "6.2 Trang Thai WiFi" },
        .children   = { PAGE_WIFI_SCAN_LIST, PAGE_WIFI_STATUS },
        .parent     = PAGE_MAIN_MENU,
    },

    [PAGE_WIFI_SCAN_LIST] = {
        .title      = "Danh Sach WiFi",
        .item_count = 0,
        .parent     = PAGE_WIFI_SETTINGS,
    },

    [PAGE_WIFI_PASS_ENTRY] = {
        .title      = "Nhap Mat Khau",
        .item_count = 0,
        .parent     = PAGE_WIFI_SCAN_LIST,
    },

    [PAGE_WIFI_STATUS] = {
        .title      = "Trang Thai WiFi",
        .item_count = 0,
        .parent     = PAGE_WIFI_SETTINGS,
    },


    /* ── System Settings ── */
    [PAGE_SYSTEM_SETTINGS] = {
        .title      = "Cai Dat He Thong",
        .item_count = 4,
        .items      = { "1.1 Ngon Ngu",
                        "1.2 Ngay Thang",
                        "1.3 Cai Dat Man Hinh",
                        "1.4 Thay Doi Mat Khau" },
        .children   = { PAGE_LANGUAGE, PAGE_DATE, PAGE_SCREEN_SETTINGS, PAGE_CHANGE_PIN },
        .parent     = PAGE_MAIN_MENU,
    },

    [PAGE_CHANGE_PIN] = {
        .title      = "Thay Doi Mat Khau",
        .item_count = 0,
        .parent     = PAGE_SYSTEM_SETTINGS,
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
                        "2.3 Che Do pH & DO" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_MAIN_MENU,
    },

    /* ── Sensor Settings (menu 4) ── */
    [PAGE_SENSOR_SETTINGS] = {
        .title      = "Cai Dat Cam Bien",
        .item_count = 2,
        .items      = { "4.1 Cam Bien pH",
                        "4.2 Cam Bien DO" },
        .children   = { PAGE_PH_SETTINGS,
                        PAGE_DO_SETTINGS },
        .parent     = PAGE_MAIN_MENU,
    },

    [PAGE_PH_SETTINGS] = {
        .title      = "Cam Bien pH",
        .item_count = 6,
        .items      = { "4.1.1 Hieu Chuan pH",
                        "4.1.2 Suc Khoe Cam Bien",
                        "4.1.3 Bo Loc So",
                        "4.1.4 Che Do Nhiet Do",
                        "4.1.5 Cai Dat Nhiet Do",
                        "4.1.6 Bu Tuyen Tinh T" },
        .children   = { PAGE_CALIBRATION,
                        PAGE_PH_HEALTH,
                        PAGE_DIGITAL_FILTER,
                        PAGE_TEMP_MODE,
                        PAGE_TEMP_SETTINGS,
                        PAGE_TEMP_LIN_COMP },
        .parent     = PAGE_SENSOR_SETTINGS,
    },

    [PAGE_PH_HEALTH] = {
        .title      = "Suc Khoe Cam Bien",
        .item_count = 0,
        .parent     = PAGE_PH_SETTINGS,
    },

    [PAGE_DO_SETTINGS] = {
        .title      = "Cam Bien DO",
        .item_count = 2,
        .items      = { "4.2.1 Hieu Chuan DO",
                        "4.2.2 Reset Hieu Chuan DO" },
        .children   = { PAGE_CAL_DO, PAGE_LEAF },
        .parent     = PAGE_SENSOR_SETTINGS,
    },

    [PAGE_CALIBRATION] = {
        .title      = "Hieu Chuan pH",
        .item_count = 3,
        .items      = { "4.1.1.1 Hieu Chuan 2D",
                        "4.1.1.2 Hieu Chuan 3D",
                        "4.1.1.3 Reset Hieu Chuan pH" },
        .children   = { PAGE_CAL_2PT, PAGE_CAL_3PT, PAGE_LEAF },
        .parent     = PAGE_PH_SETTINGS,
    },

    [PAGE_CAL_DO] = {
        .title      = "Hieu Chuan DO",
        .item_count = 3,
        .items      = { "1. Hieu Chuan Diem 0",
                        "2. Hieu Chuan Do Doc",
                        "3. Hieu Chinh Nhiet Do" },
        .children   = { PAGE_CAL_DO_EXEC, PAGE_CAL_DO_EXEC, PAGE_CAL_DO_TEMP },
        .parent     = PAGE_DO_SETTINGS,
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
        .items      = { "4.1.2.1 Thap",
                        "4.1.2.2 Vua",
                        "4.1.2.3 Cao" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_PH_SETTINGS,
    },

    [PAGE_TEMP_MODE] = {
        .title      = "Che Do Nhiet Do",
        .item_count = 4,
        .items      = { "4.1.3.1 ATC  C",
                        "4.1.3.2 MTC  C",
                        "4.1.3.3 ATF  F",
                        "4.1.3.4 MTF  F" },
        .children   = { PAGE_LEAF, PAGE_LEAF, PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_PH_SETTINGS,
    },

    [PAGE_TEMP_SETTINGS] = {
        .title      = "Cai Dat Nhiet Do",
        .item_count = 0,
        .parent     = PAGE_PH_SETTINGS,
    },

    [PAGE_TEMP_LIN_COMP] = {
        .title      = "Bu Tuyen Tinh T",
        .item_count = 0,
        .parent     = PAGE_PH_SETTINGS,
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

    [PAGE_RESET_SENSOR] = {
        .title      = "Reset Cam Bien",
        .item_count = 2,
        .items      = { "1. Reset pH",
                        "2. Reset DO" },
        .children   = { PAGE_LEAF, PAGE_LEAF },
        .parent     = PAGE_CALIBRATION,
    },

    [PAGE_CONFIRM_RESET] = {
        .title      = "Xac Nhan Reset",
        .item_count = 0,
        .parent     = PAGE_CALIBRATION,
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

typedef enum {
    PIN_MODE_UNLOCK = 0,     /**< Nhập mật khẩu để truy cập Modbus / Cảm biến */
    PIN_MODE_CHANGE_OLD,    /**< Bước 1 khi thay đổi MK: Nhập MK cũ */
    PIN_MODE_CHANGE_NEW,    /**< Bước 2 khi thay đổi MK: Nhập MK mới */
} pin_mode_t;

static pin_mode_t s_pin_mode = PIN_MODE_UNLOCK;
static char    s_menu_pin_stored[MENU_PIN_LEN + 1] = MENU_PIN_DEFAULT;
static uint8_t s_pin_entry[MENU_PIN_LEN] = {0, 0, 0, 0};
static uint8_t s_pin_cursor = 0;
static bool    s_pin_show_error = false;
static menu_page_t s_pin_target_page = PAGE_MODBUS_SETTINGS;
static uint8_t s_pin_reveal = 0;   /**< so lan ve con lai de hien so truoc khi an */

#define MENU_PIN_REVEAL_TICKS 16   /**< ~0.8s (16 x 50ms) hien so roi an thanh * */

static float s_manual_temp_edit = 25.0f;
static float s_temp_offset_edit = 0.0f;
static float s_temp_alpha_edit  = 0.0f;
static uint32_t s_menu_scroll_ticks = 0;

static uint8_t s_modbus_edit_port = 1;
static uint8_t s_modbus_addr_edit = 1;

static struct {
    float target_ph;
    float min_mv;
    float max_mv;
    uint8_t cal_type;   // 2 = 2-point, 3 = 3-point
    uint8_t group_idx;  // 0 = 2PT, 1 = G1, 2 = G2
} s_cal_exec;

static struct {
    uint8_t do_cal_type; // 0 = Zero, 1 = Slope
} s_do_cal_exec;

typedef enum {
    RESET_TARGET_PH = 0,
    RESET_TARGET_DO
} reset_target_t;

static reset_target_t s_confirm_reset_target = RESET_TARGET_PH;
static menu_page_t    s_confirm_reset_prev_page = PAGE_CALIBRATION;

static float s_do_temp_cal_edit = 25.0f;
static uint16_t s_history_view_offset = 0; // 0 = mới nhất, 1 = kế mới nhất...
static uint8_t s_device_info_scroll_offset = 0;

/* =====================================================================
 * Trạng thái WiFi LCD (Khai báo biến toàn cục nội bộ)
 * ===================================================================== */
static wifi_ap_record_t s_wifi_scan_aps[16];
static uint16_t s_wifi_scan_ap_count = 0;
static int8_t   s_wifi_scan_selected_idx = 0;
static uint8_t  s_wifi_scan_scroll_offset = 0;

static char s_wifi_selected_ssid[33] = {0};
static char s_wifi_pass_input[65] = {0};
static uint8_t s_wifi_pass_len = 0;

static const char s_char_picker_set[] = 
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    "!@#$%^&*()_+-=[]{}|;:,.<>?/ ";
#define CHAR_PICKER_SET_LEN (sizeof(s_char_picker_set) - 1)

static uint8_t s_char_picker_set_idx = 0;

typedef enum {
    WIFI_PASS_FOCUS_PICKER = 0,
    WIFI_PASS_FOCUS_DEL,
    WIFI_PASS_FOCUS_CONNECT,
    WIFI_PASS_FOCUS_COUNT
} wifi_pass_focus_t;

static wifi_pass_focus_t s_wifi_pass_focus = WIFI_PASS_FOCUS_PICKER;

static void menu_render_history_log(void)

{
    uint16_t total_count = Fram_Log_Get_Count();

    if (total_count == 0) {
        const char *msg = (g_sys_lang == LANG_VI) ? "Chua co du lieu" : "No Data Stored";
        uint8_t msg_w = (uint8_t)(strlen(msg) * 6);
        uint8_t msg_x = (uint8_t)((128 - msg_w) / 2);
        LCD_DrawString(msg_x, 28, msg, LCD_COLOR_ON);
        return;
    }

    if (s_history_view_offset >= total_count) {
        s_history_view_offset = total_count - 1;
    }

    uint16_t rel_idx = (total_count - 1) - s_history_view_offset;

    EnvLogRecord_t rec;
    memset(&rec, 0, sizeof(rec));

    if (!Fram_Log_Read_Record(rel_idx, &rec)) {
        const char *err = (g_sys_lang == LANG_VI) ? "Loi doc FRAM!" : "FRAM Read Error!";
        LCD_DrawString(16, 28, err, LCD_COLOR_ON);
        return;
    }

    struct tm tm_rec;
    time_t ts = (time_t)rec.timestamp;
    localtime_r(&ts, &tm_rec);

    char line_hdr[64];
    snprintf(line_hdr, sizeof(line_hdr), "#%u/%u %02d/%02d %02d:%02d",
             (unsigned int)(s_history_view_offset + 1), (unsigned int)total_count,
             tm_rec.tm_mday, tm_rec.tm_mon + 1, tm_rec.tm_hour, tm_rec.tm_min);
    LCD_DrawString(2, 13, line_hdr, LCD_COLOR_ON);

    char line_ph[32];
    snprintf(line_ph, sizeof(line_ph), "pH   : %.2f", (float)rec.ph_x100 / 100.0f);
    LCD_DrawString(2, 23, line_ph, LCD_COLOR_ON);

    char line_temp[32];
    snprintf(line_temp, sizeof(line_temp), "Temp : %.2f \xB0" "C", (float)rec.temp_x100 / 100.0f);
    LCD_DrawString(2, 33, line_temp, LCD_COLOR_ON);

    char line_do[32];
    snprintf(line_do, sizeof(line_do), "DO   : %.2f mg/L", (float)rec.do_x100 / 100.0f);
    LCD_DrawString(2, 43, line_do, LCD_COLOR_ON);
}

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
    if (target == PAGE_CHANGE_PIN) {
        s_pin_mode = PIN_MODE_CHANGE_OLD;
    } else {
        s_pin_mode = PIN_MODE_UNLOCK;
    }
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
    if (page == PAGE_CHANGE_PIN) {
        menu_pin_begin_entry(PAGE_CHANGE_PIN);
        return;
    }

    if (page == PAGE_MODBUS_EDIT_ADDR ||
        page == PAGE_MODBUS_SELECT_BAUD ||
        page == PAGE_MODBUS_SELECT_PARITY ||
        page == PAGE_MODBUS_SELECT_STOP) {
        if (g_menu.current_page == PAGE_MODBUS_PORT1) {
            s_modbus_edit_port = 1;
        } else if (g_menu.current_page == PAGE_MODBUS_PORT2) {
            s_modbus_edit_port = 2;
        }
        if (page == PAGE_MODBUS_EDIT_ADDR) {
            s_modbus_addr_edit = (s_modbus_edit_port == 1) ? g_mb1_addr : g_mb2_addr;
        }
    } else if (page == PAGE_CAL_DO_EXEC) {
        s_do_cal_exec.do_cal_type = g_menu.selected;
    } else if (page == PAGE_CAL_DO_TEMP) {
        PH_Temp_Sensor_Status_t status = Get_Sensor_Status();
        s_do_temp_cal_edit = status.do_temp_c;
        if (s_do_temp_cal_edit < 0.0f || s_do_temp_cal_edit > 100.0f) {
            s_do_temp_cal_edit = 25.0f;
        }
    }

    g_menu.current_page  = page;
    g_menu.selected      = 0;
    g_menu.scroll_offset = 0;
    s_menu_scroll_ticks  = 0;
    if (page == PAGE_TIME_SETTINGS) {
        time_edit_begin();     /* nap gio hien tai vao buffer chinh sua */
    } else if (page == PAGE_TEMP_SETTINGS) {
        bool is_f = (g_temp_mode == TEMP_MODE_ATC_F || g_temp_mode == TEMP_MODE_MTC_F);
        if (g_temp_mode == TEMP_MODE_MTC_C || g_temp_mode == TEMP_MODE_MTC_F) {
            if (is_f) {
                s_manual_temp_edit = g_manual_temp * 1.8f + 32.0f; // Đổi sang độ F để hiển thị/chỉnh sửa
            } else {
                s_manual_temp_edit = g_manual_temp;
            }
        } else { // Chế độ ATC (chỉnh offset)
            if (is_f) {
                s_temp_offset_edit = g_temp_offset * 1.8f;         // Đổi độ lệch sang độ F (delta_F = delta_C * 1.8)
            } else {
                s_temp_offset_edit = g_temp_offset;
            }
        }
    } else if (page == PAGE_TEMP_LIN_COMP) {
        s_temp_alpha_edit = g_temp_alpha;
    } else if (page == PAGE_WIFI_SCAN_LIST) {
        s_wifi_scan_selected_idx = 0;
        s_wifi_scan_scroll_offset = 0;
        wifi_config_manager_trigger_scan();
    } else if (page == PAGE_WIFI_PASS_ENTRY) {
        memset(s_wifi_pass_input, 0, sizeof(s_wifi_pass_input));
        s_wifi_pass_len = 0;
        s_char_picker_set_idx = 0;
        s_wifi_pass_focus = WIFI_PASS_FOCUS_PICKER;
    } else if (page == PAGE_DEVICE_INFO) {
        s_device_info_scroll_offset = 0;
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
static TickType_t s_btn_press_tick[BTN_COUNT] = {0};
static TickType_t s_btn_last_repeat_tick[BTN_COUNT] = {0};

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
                if (raw) {
                    s_btn_press_tick[i] = xTaskGetTickCount();
                    s_btn_last_repeat_tick[i] = s_btn_press_tick[i];
                } else {
                    s_btn_press_tick[i] = 0;
                    s_btn_last_repeat_tick[i] = 0;
                }
            }
        } else {
            s_btn_debounce_counter[i] = 0;
        }
    }

    // Nhấn giữ nút ENTER trong 7 giây để reset hệ thống bằng phần mềm
    static TickType_t s_enter_press_start_tick = 0;
    static bool s_enter_was_pressed = false;

    if (s_btn_state[BTN_IDX_ENTER]) {
        if (!s_enter_was_pressed) {
            s_enter_press_start_tick = xTaskGetTickCount();
            s_enter_was_pressed = true;
        } else {
            TickType_t elapsed = xTaskGetTickCount() - s_enter_press_start_tick;
            if (elapsed >= pdMS_TO_TICKS(7000)) {
                ESP_LOGW(TAG_MENU, "ENTER button held for 7s. Restarting system...");
                LCD_Clear();
                if (g_sys_lang == LANG_VI) {
                    LCD_DrawString(22, 20, "DANG KHOI DONG", LCD_COLOR_ON);
                    LCD_DrawString(19, 32, "LAI HE THONG...", LCD_COLOR_ON);
                } else {
                    LCD_DrawString(7, 26, "REBOOTING SYSTEM...", LCD_COLOR_ON);
                }
                LCD_Flush();
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
            }
        }
    } else {
        s_enter_was_pressed = false;
    }
}

/**
 * @brief  Trả về true nếu nút vừa được nhấn (phát hiện sườn lên đã chống rung)
 */
static bool btn_edge(btn_idx_t idx)
{
    if (idx >= BTN_COUNT) return false;
    if (s_btn_simulated[idx]) {
        s_btn_simulated[idx] = false;
        return true;
    }
    return s_btn_state[idx] && !s_btn_prev_state[idx];
}

/**
 * @brief  Trả về true nếu nút vừa nhấn (edge) HOẶC đang nhấn giữ tự động lặp (auto-repeat)
 * @param  idx: Nút cần kiểm tra
 * @param  initial_delay_ms: Thời gian giữ tối thiểu trước khi bắt đầu lặp (ms)
 * @param  repeat_interval_ms: Chu kỳ giữa các lần lặp (ms)
 */
static bool btn_edge_or_repeat(btn_idx_t idx, uint32_t initial_delay_ms, uint32_t repeat_interval_ms)
{
    if (idx >= BTN_COUNT) return false;

    // Giả lập
    if (s_btn_simulated[idx]) {
        s_btn_simulated[idx] = false;
        return true;
    }

    // Nhấn lần đầu (cạnh lên)
    if (s_btn_state[idx] && !s_btn_prev_state[idx]) {
        return true;
    }

    // Nhấn giữ (auto-repeat) với tốc độ ổn định
    if (s_btn_state[idx] && s_btn_press_tick[idx] != 0) {
        TickType_t now = xTaskGetTickCount();
        TickType_t elapsed_press = (now >= s_btn_press_tick[idx]) ? 
                                   (now - s_btn_press_tick[idx]) : 
                                   (portMAX_DELAY - s_btn_press_tick[idx] + now);

        if (elapsed_press >= pdMS_TO_TICKS(initial_delay_ms)) {
            TickType_t elapsed_repeat = (now >= s_btn_last_repeat_tick[idx]) ? 
                                        (now - s_btn_last_repeat_tick[idx]) : 
                                        (portMAX_DELAY - s_btn_last_repeat_tick[idx] + now);

            if (elapsed_repeat >= pdMS_TO_TICKS(repeat_interval_ms)) {
                s_btn_last_repeat_tick[idx] = now;
                return true;
            }
        }
    }

    return false;
}

static void menu_handle_history_log_buttons(void)
{
    const page_def_t *page = (g_sys_lang == LANG_VI) ? &s_pages_vi[g_menu.current_page] : &s_pages_en[g_menu.current_page];
    uint16_t total_count = Fram_Log_Get_Count();

    if (btn_edge(BTN_IDX_ESC)) {
        s_history_view_offset = 0;
        goto_page(page->parent);
        g_lcd_need_redraw = true;
        return;
    }

    // Nút DOWN (▼): Cuộn về quá khứ (#1 -> #2 -> #3 ... -> #total_count)
    if (btn_edge(BTN_IDX_DOWN)) {
        if (total_count > 0) {
            if (s_history_view_offset < total_count - 1) {
                s_history_view_offset++;
            } else {
                s_history_view_offset = 0; // Đang ở bản ghi cũ nhất (#total_count), bấm DOWN sẽ quay về mới nhất (#1)
            }
            g_lcd_need_redraw = true;
        }
    }

    // Nút UP (▲): Đi từ quá khứ về hiện tại (#total_count -> ... -> #1). Nếu đang ở #1, bấm UP sẽ quay về cũ nhất (#total_count)
    if (btn_edge(BTN_IDX_UP)) {
        if (total_count > 0) {
            if (s_history_view_offset > 0) {
                s_history_view_offset--;
            } else {
                s_history_view_offset = total_count - 1; // Đang ở #1, bấm UP tự động nhảy tới cũ nhất (#total_count)
            }
            g_lcd_need_redraw = true;
        }
    }
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

static void menu_show_alert_dialog(const char *title, const char *msg, bool success)
{
    // 1. Xóa trắng vùng không gian làm popup ở giữa màn hình (rộng 104px, cao 36px)
    LCD_FillRect(12, 14, 104, 36, LCD_COLOR_OFF);

    // 2. Vẽ viền hình chữ nhật bên ngoài
    LCD_DrawRect(12, 14, 104, 36, LCD_COLOR_ON);

    // 3. Vẽ viền hình chữ nhật bên trong (tạo hiệu ứng viền đôi 3D nổi bật)
    LCD_DrawRect(14, 16, 100, 32, LCD_COLOR_ON);
    
    // 4. Vẽ thanh tiêu đề đảo màu (nền đen chữ trắng)
    LCD_FillRect(15, 17, 98, 9, LCD_COLOR_ON);

    // 5. Tính toán vị trí X để căn giữa chuỗi Tiêu đề (Title)
    int title_len = strlen(title);
    int title_x = 12 + (104 - title_len * 6) / 2;
    LCD_DrawString(title_x, 18, title, LCD_COLOR_OFF); // Chữ trắng trên nền đen
    
    // 6. Tính toán vị trí X để căn giữa chuỗi Thông báo (Message)
    int msg_len = strlen(msg);
    int msg_x = 12 + (104 - msg_len * 6) / 2;
    LCD_DrawString(msg_x, 34, msg, LCD_COLOR_ON);    // Chữ đen trên nền trắng
    
    // 7. Gửi dữ liệu từ Framebuffer lên màn hình LCD & Dừng 1.5 giây
    LCD_Flush();
    vTaskDelay(pdMS_TO_TICKS(1500)); // Khóa màn hình hiển thị thông báo trong 1.5 giây
}

/* =====================================================================
 * Trạng thái WiFi LCD & Forward Declarations
 * ===================================================================== */
static void draw_arrow_up(uint8_t x, uint8_t y, uint8_t color);
static void draw_arrow_down(uint8_t x, uint8_t y, uint8_t color);
static void draw_arrow_right(uint8_t x, uint8_t y, uint8_t color);
static void draw_arrow_up_large(uint8_t x, uint8_t y, uint8_t color);
static void draw_arrow_down_large(uint8_t x, uint8_t y, uint8_t color);
static void draw_arrow_right_large(uint8_t x, uint8_t y, uint8_t color);

/* ── 1. Màn hình Danh sách WiFi ── */

static void menu_render_wifi_scan_list(void)
{
    LCD_Clear();

    LCD_FillRect(0, 0, 128, TITLE_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(4, 2, g_sys_lang == LANG_VI ? "Danh Sach WiFi" : "WiFi Networks", LCD_COLOR_OFF);

    wifi_scan_state_t scan_st = wifi_config_manager_get_scan_state();

    if (scan_st == WIFI_SCAN_STATE_SCANNING) {
        const char *msg = (g_sys_lang == LANG_VI) ? "Dang quet WiFi..." : "Scanning WiFi...";
        uint8_t msg_w = strlen(msg) * 6;
        uint8_t msg_x = (128 - msg_w) / 2;
        LCD_DrawString(msg_x, 26, msg, LCD_COLOR_ON);
        g_lcd_need_redraw = true;
    } else {
        s_wifi_scan_ap_count = wifi_config_manager_get_scan_results(s_wifi_scan_aps, 16);

        if (s_wifi_scan_ap_count == 0) {
            const char *msg1 = (g_sys_lang == LANG_VI) ? "Khong tim thay WiFi!" : "No WiFi Found!";
            const char *msg2 = (g_sys_lang == LANG_VI) ? "ENT: Quet Lai" : "ENT: Rescan";
            uint8_t w1 = strlen(msg1) * 6;
            uint8_t w2 = strlen(msg2) * 6;
            LCD_DrawString((128 - w1) / 2, 22, msg1, LCD_COLOR_ON);
            LCD_DrawString((128 - w2) / 2, 34, msg2, LCD_COLOR_ON);
        } else {
            uint8_t visible = s_wifi_scan_ap_count - s_wifi_scan_scroll_offset;
            if (visible > VISIBLE_ITEMS) visible = VISIBLE_ITEMS;

            for (uint8_t i = 0; i < visible; i++) {
                uint8_t idx = s_wifi_scan_scroll_offset + i;
                uint8_t y = ITEMS_Y_START + i * ITEM_ROW_H;
                bool is_sel = (idx == s_wifi_scan_selected_idx);

                char row_buf[32];
                snprintf(row_buf, sizeof(row_buf), "%.13s (%d)", (char*)s_wifi_scan_aps[idx].ssid, s_wifi_scan_aps[idx].rssi);

                if (is_sel) {
                    LCD_FillRect(0, y, 128, ITEM_ROW_H, LCD_COLOR_ON);
                    LCD_DrawStringScroll(4, y + 1, row_buf, 120, 0, LCD_COLOR_OFF);
                    draw_arrow_right(121, y + 2, LCD_COLOR_OFF);
                } else {
                    LCD_DrawStringScroll(4, y + 1, row_buf, 120, 0, LCD_COLOR_ON);
                }
            }

            if (s_wifi_scan_scroll_offset > 0) {
                draw_arrow_up(120, ITEMS_Y_START + 1, LCD_COLOR_ON);
            }
            if (s_wifi_scan_scroll_offset + VISIBLE_ITEMS < s_wifi_scan_ap_count) {
                draw_arrow_down(120, STATUS_BAR_Y - 6, LCD_COLOR_ON);
            }
        }
    }

    LCD_FillRect(0, STATUS_BAR_Y, 128, STATUS_BAR_H, LCD_COLOR_ON);
    draw_arrow_down_large(8, STATUS_BAR_Y + 3, LCD_COLOR_OFF);
    draw_arrow_up_large(32, STATUS_BAR_Y + 3, LCD_COLOR_OFF);
    LCD_DrawString(80, STATUS_BAR_Y + 3, "ESC", LCD_COLOR_OFF);
    LCD_DrawString(104, STATUS_BAR_Y + 3, "ENT", LCD_COLOR_OFF);

    LCD_Flush();
}

static void menu_handle_wifi_scan_list_buttons(void)
{
    if (btn_edge(BTN_IDX_ESC)) {
        goto_page(PAGE_WIFI_SETTINGS);
        g_lcd_need_redraw = true;
        return;
    }

    wifi_scan_state_t scan_st = wifi_config_manager_get_scan_state();
    if (scan_st == WIFI_SCAN_STATE_SCANNING) {
        return;
    }

    if (s_wifi_scan_ap_count == 0) {
        if (btn_edge(BTN_IDX_ENTER)) {
            wifi_config_manager_trigger_scan();
            g_lcd_need_redraw = true;
        }
        return;
    }

    if (btn_edge(BTN_IDX_UP)) {
        if (s_wifi_scan_selected_idx > 0) {
            s_wifi_scan_selected_idx--;
            if (s_wifi_scan_selected_idx < s_wifi_scan_scroll_offset) {
                s_wifi_scan_scroll_offset = s_wifi_scan_selected_idx;
            }
            g_lcd_need_redraw = true;
        }
    }

    if (btn_edge(BTN_IDX_DOWN)) {
        if (s_wifi_scan_selected_idx < s_wifi_scan_ap_count - 1) {
            s_wifi_scan_selected_idx++;
            if (s_wifi_scan_selected_idx >= s_wifi_scan_scroll_offset + VISIBLE_ITEMS) {
                s_wifi_scan_scroll_offset = s_wifi_scan_selected_idx - VISIBLE_ITEMS + 1;
            }
            g_lcd_need_redraw = true;
        }
    }

    if (btn_edge(BTN_IDX_ENTER)) {
        if (s_wifi_scan_selected_idx >= 0 && s_wifi_scan_selected_idx < s_wifi_scan_ap_count) {
            strncpy(s_wifi_selected_ssid, (char*)s_wifi_scan_aps[s_wifi_scan_selected_idx].ssid, sizeof(s_wifi_selected_ssid) - 1);
            s_wifi_selected_ssid[sizeof(s_wifi_selected_ssid) - 1] = '\0';
            goto_page(PAGE_WIFI_PASS_ENTRY);
            g_lcd_need_redraw = true;
        }
    }

    btn_edge(BTN_IDX_RIGHT);
}

/* ── 2. Màn hình Nhập Mật Khẩu WiFi (Character Picker / Spin-Box) ── */
static void menu_render_wifi_pass_entry(void)
{
    LCD_Clear();

    LCD_FillRect(0, 0, 128, TITLE_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(4, 2, g_sys_lang == LANG_VI ? "Nhap Mat Khau" : "WiFi Password", LCD_COLOR_OFF);

    char ssid_line[32];
    snprintf(ssid_line, sizeof(ssid_line), "SSID: %.14s", s_wifi_selected_ssid);
    LCD_DrawString(2, 13, ssid_line, LCD_COLOR_ON);

    char pass_line[80];
    if (s_wifi_pass_len <= 14) {
        snprintf(pass_line, sizeof(pass_line), "Pass: %s_", s_wifi_pass_input);
    } else {
        // Mật khẩu dài hơn 14 ký tự: cuộn hiển thị 12 ký tự đuôi mới nhất
        snprintf(pass_line, sizeof(pass_line), "Pass: ..%s_", &s_wifi_pass_input[s_wifi_pass_len - 12]);
    }
    LCD_DrawString(2, 23, pass_line, LCD_COLOR_ON);

    char cur_char = s_char_picker_set[s_char_picker_set_idx];
    char picker_str[16];
    if (cur_char == ' ') {
        snprintf(picker_str, sizeof(picker_str), "[ SPC ]");
    } else {
        snprintf(picker_str, sizeof(picker_str), "[ %c ]", cur_char);
    }

    if (s_wifi_pass_focus == WIFI_PASS_FOCUS_PICKER) {
        LCD_FillRect(2, 34, 45, 12, LCD_COLOR_ON);
        LCD_DrawString(4, 36, picker_str, LCD_COLOR_OFF);
    } else {
        LCD_DrawRect(2, 34, 45, 12, LCD_COLOR_ON);
        LCD_DrawString(4, 36, picker_str, LCD_COLOR_ON);
    }

    if (s_wifi_pass_focus == WIFI_PASS_FOCUS_DEL) {
        LCD_FillRect(52, 34, 32, 12, LCD_COLOR_ON);
        LCD_DrawString(54, 36, "[DEL]", LCD_COLOR_OFF);
    } else {
        LCD_DrawRect(52, 34, 32, 12, LCD_COLOR_ON);
        LCD_DrawString(54, 36, "[DEL]", LCD_COLOR_ON);
    }

    const char *conn_lbl = (g_sys_lang == LANG_VI) ? "[LUU]" : "[OK]";
    if (s_wifi_pass_focus == WIFI_PASS_FOCUS_CONNECT) {
        LCD_FillRect(88, 34, 38, 12, LCD_COLOR_ON);
        LCD_DrawString(90, 36, conn_lbl, LCD_COLOR_OFF);
    } else {
        LCD_DrawRect(88, 34, 38, 12, LCD_COLOR_ON);
        LCD_DrawString(90, 36, conn_lbl, LCD_COLOR_ON);
    }

    LCD_FillRect(0, STATUS_BAR_Y, 128, STATUS_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(4, STATUS_BAR_Y + 3, "ESC", LCD_COLOR_OFF);
    LCD_DrawString(34, STATUS_BAR_Y + 3, "-/+", LCD_COLOR_OFF);
    draw_arrow_right_large(64, STATUS_BAR_Y + 3, LCD_COLOR_OFF);
    LCD_DrawString(90, STATUS_BAR_Y + 3, "ENT", LCD_COLOR_OFF);

    LCD_Flush();
}

static void menu_handle_wifi_pass_entry_buttons(void)
{
    if (btn_edge(BTN_IDX_ESC)) {
        goto_page(PAGE_WIFI_SCAN_LIST);
        g_lcd_need_redraw = true;
        return;
    }

    if (btn_edge(BTN_IDX_RIGHT)) {
        s_wifi_pass_focus = (wifi_pass_focus_t)((s_wifi_pass_focus + 1) % WIFI_PASS_FOCUS_COUNT);
        g_lcd_need_redraw = true;
        return;
    }

    if (s_wifi_pass_focus == WIFI_PASS_FOCUS_PICKER) {
        // Nhấn giữ nút UP hoặc DOWN để cuộn đổi ký tự liên tục
        if (btn_edge_or_repeat(BTN_IDX_UP, 350, 90)) {
            s_char_picker_set_idx = (s_char_picker_set_idx + 1) % CHAR_PICKER_SET_LEN;
            g_lcd_need_redraw = true;
        }

        if (btn_edge_or_repeat(BTN_IDX_DOWN, 350, 90)) {
            s_char_picker_set_idx = (s_char_picker_set_idx + CHAR_PICKER_SET_LEN - 1) % CHAR_PICKER_SET_LEN;
            g_lcd_need_redraw = true;
        }
    } else {
        if (btn_edge(BTN_IDX_UP)) {
            s_wifi_pass_focus = (wifi_pass_focus_t)((s_wifi_pass_focus + WIFI_PASS_FOCUS_COUNT - 1) % WIFI_PASS_FOCUS_COUNT);
            g_lcd_need_redraw = true;
        }

        if (btn_edge(BTN_IDX_DOWN)) {
            s_wifi_pass_focus = (wifi_pass_focus_t)((s_wifi_pass_focus + 1) % WIFI_PASS_FOCUS_COUNT);
            g_lcd_need_redraw = true;
        }
    }

    if (s_wifi_pass_focus == WIFI_PASS_FOCUS_DEL) {
        // Nhấn hoặc giữ ENTER trên nút [DEL] để xóa ký tự nhanh
        if (btn_edge_or_repeat(BTN_IDX_ENTER, 400, 100)) {
            if (s_wifi_pass_len > 0) {
                s_wifi_pass_len--;
                s_wifi_pass_input[s_wifi_pass_len] = '\0';
                g_lcd_need_redraw = true;
            }
        }
    } else if (btn_edge(BTN_IDX_ENTER)) {
        if (s_wifi_pass_focus == WIFI_PASS_FOCUS_PICKER) {
            if (s_wifi_pass_len < 64) {
                s_wifi_pass_input[s_wifi_pass_len] = s_char_picker_set[s_char_picker_set_idx];
                s_wifi_pass_len++;
                s_wifi_pass_input[s_wifi_pass_len] = '\0';
            }
            g_lcd_need_redraw = true;
        } else if (s_wifi_pass_focus == WIFI_PASS_FOCUS_CONNECT) {
            wifi_config_manager_save(s_wifi_selected_ssid, s_wifi_pass_input);
            wifi_config_manager_schedule_connect();
            menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Ket Noi WiFi" : "WiFi Connect", g_sys_lang == LANG_VI ? "Dang Ket Noi..." : "Connecting...", true);
            goto_page(PAGE_WIFI_STATUS);
            g_lcd_need_redraw = true;
        }
    }
}

/* ── 3. Màn hình Trạng Thái WiFi ── */
static void menu_render_wifi_status(void)
{
    LCD_Clear();

    LCD_FillRect(0, 0, 128, TITLE_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(4, 2, g_sys_lang == LANG_VI ? "Trang Thai WiFi" : "WiFi Status", LCD_COLOR_OFF);

    char line1[32], line2[32], line3[32], line4[32];

    char saved_ssid[32] = {0};
    char saved_pass[64] = {0};
    wifi_config_manager_load(saved_ssid, sizeof(saved_ssid), saved_pass, sizeof(saved_pass));

    bool is_conn = Sys_Info.isWifiConnected;
    snprintf(line1, sizeof(line1), "Status: %s", is_conn ? "CONNECTED" : "CONNECTING...");
    snprintf(line2, sizeof(line2), "SSID: %.14s", saved_ssid[0] ? saved_ssid : "None");

    if (is_conn) {
        esp_netif_ip_info_t ip_info;
        esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) {
            snprintf(line3, sizeof(line3), "IP: " IPSTR, IP2STR(&ip_info.ip));
        } else {
            snprintf(line3, sizeof(line3), "IP: Got IP");
        }
    } else {
        snprintf(line3, sizeof(line3), "IP: 0.0.0.0");
    }

    snprintf(line4, sizeof(line4), "AP: 192.168.14.1");

    LCD_DrawString(2, 13, line1, LCD_COLOR_ON);
    LCD_DrawString(2, 23, line2, LCD_COLOR_ON);
    LCD_DrawString(2, 33, line3, LCD_COLOR_ON);
    LCD_DrawString(2, 42, line4, LCD_COLOR_ON);

    LCD_FillRect(0, STATUS_BAR_Y, 128, STATUS_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(8, STATUS_BAR_Y + 3, "ESC", LCD_COLOR_OFF);
    LCD_Flush();
}

static void menu_handle_wifi_status_buttons(void)
{
    if (btn_edge(BTN_IDX_ESC)) {
        goto_page(PAGE_WIFI_SETTINGS);
        g_lcd_need_redraw = true;
    }
}

/* ── 4. Màn hình Thông Tin Thiết Bị (Device Info - Grouped Scrollable View) ── */

static const char* get_reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_POWERON:   return "Power-on Reset";
        case ESP_RST_EXT:       return "External Pin";
        case ESP_RST_SW:        return "Software Reset";
        case ESP_RST_PANIC:     return "Exception Panic";
        case ESP_RST_INT_WDT:   return "Int Watchdog";
        case ESP_RST_TASK_WDT:  return "Task Watchdog";
        case ESP_RST_WDT:       return "Other Watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep Sleep";
        case ESP_RST_BROWNOUT:  return "Brownout Reset";
        case ESP_RST_SDIO:      return "SDIO Reset";
        default:                return "Unknown Reset";
    }
}

typedef struct {
    char text[44];
    bool is_header;
} device_info_line_t;

static void menu_render_device_info(void)
{
    LCD_Clear();

    LCD_FillRect(0, 0, 128, TITLE_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(4, 2, g_sys_lang == LANG_VI ? "Thong Tin Thiet Bi" : "Device Info", LCD_COLOR_OFF);

    device_info_line_t lines[24];
    uint8_t count = 0;

    /* ── Nhóm 1: Mạng & Cloud Azure ── */
    snprintf(lines[count].text, sizeof(lines[count].text),
             g_sys_lang == LANG_VI ? "--- 1. MANG & AZURE ---" : "--- 1. NET & AZURE ---");
    lines[count++].is_header = true;

    bool is_wifi = Sys_Info.isWifiConnected;
    char wifi_ssid[32] = {0};
    int8_t rssi = -100;
    if (is_wifi) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            strncpy(wifi_ssid, (char*)ap_info.ssid, sizeof(wifi_ssid) - 1);
            rssi = ap_info.rssi;
        }
    }
    if (!wifi_ssid[0]) {
        wifi_config_manager_load(wifi_ssid, sizeof(wifi_ssid), NULL, 0);
    }

    snprintf(lines[count].text, sizeof(lines[count].text),
             "WiFi: %s", is_wifi ? "Connected" : "Disconnected");
    lines[count++].is_header = false;

    snprintf(lines[count].text, sizeof(lines[count].text),
             "SSID: %.14s", wifi_ssid[0] ? wifi_ssid : "None");
    lines[count++].is_header = false;

    if (is_wifi) {
        snprintf(lines[count].text, sizeof(lines[count].text), "Signal: %d dBm", rssi);
    } else {
        snprintf(lines[count].text, sizeof(lines[count].text), "Signal: No Signal");
    }
    lines[count++].is_header = false;

    if (is_wifi) {
        esp_netif_ip_info_t ip_info;
        esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) {
            snprintf(lines[count].text, sizeof(lines[count].text), "IP: " IPSTR, IP2STR(&ip_info.ip));
        } else {
            snprintf(lines[count].text, sizeof(lines[count].text), "IP: Got IP");
        }
    } else {
        snprintf(lines[count].text, sizeof(lines[count].text), "IP: 0.0.0.0");
    }
    lines[count++].is_header = false;

    snprintf(lines[count].text, sizeof(lines[count].text),
             "Azure: %s", IoTHubHandle.isAzureInitialized ? "Connected" : "Disconnected");
    lines[count++].is_header = false;

    const char *dev_id = IoTHubHandle.deviceId[0] ? IoTHubHandle.deviceId : SYS_IOT_HUB_DEVICE_ID_DEFAULT;
    snprintf(lines[count].text, sizeof(lines[count].text), "Dev ID: %.12s", dev_id);
    lines[count++].is_header = false;

    const char *hub_host = IoTHubHandle.hostName[0] ? IoTHubHandle.hostName : SYS_IOT_HUB_HOST_NAME_DEFAULT;
    snprintf(lines[count].text, sizeof(lines[count].text), "Hub: %.15s", hub_host);
    lines[count++].is_header = false;

    /* ── Nhóm 2: Hệ thống & OTA ── */
    snprintf(lines[count].text, sizeof(lines[count].text),
             g_sys_lang == LANG_VI ? "--- 2. HE THONG & OTA ---" : "--- 2. SYSTEM & OTA ---");
    lines[count++].is_header = true;

    uint32_t free_heap = esp_get_free_heap_size();
    snprintf(lines[count].text, sizeof(lines[count].text), "Free RAM: %.1f KB", free_heap / 1024.0f);
    lines[count++].is_header = false;

    uint64_t uptime_sec = esp_timer_get_time() / 1000000ULL;
    uint32_t days = uptime_sec / 86400ULL;
    uint32_t hours = (uptime_sec % 86400ULL) / 3600ULL;
    uint32_t mins = (uptime_sec % 3600ULL) / 60ULL;
    uint32_t secs = uptime_sec % 60ULL;

    if (days > 0) {
        snprintf(lines[count].text, sizeof(lines[count].text), "Uptime: %lud %luh %lum", (unsigned long)days, (unsigned long)hours, (unsigned long)mins);
    } else {
        snprintf(lines[count].text, sizeof(lines[count].text), "Uptime: %luh %lum %lus", (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    }
    lines[count++].is_header = false;

    snprintf(lines[count].text, sizeof(lines[count].text), "Firmware: v%s", VERSION);
    lines[count++].is_header = false;

    const esp_partition_t *running = esp_ota_get_running_partition();
    snprintf(lines[count].text, sizeof(lines[count].text), "Active Slot: %s", running ? running->label : "factory");
    lines[count++].is_header = false;

    snprintf(lines[count].text, sizeof(lines[count].text), "OTA Status: %s", User_Ota_Get_Status_String());
    lines[count++].is_header = false;

    /* ── Nhóm 3: Thời gian & Chẩn đoán Nguồn ── */
    snprintf(lines[count].text, sizeof(lines[count].text),
             g_sys_lang == LANG_VI ? "--- 3. GIO & NGUON ---" : "--- 3. TIME & RESET ---");
    lines[count++].is_header = true;

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    snprintf(lines[count].text, sizeof(lines[count].text),
             "Time: %02d/%02d %02d:%02d:%02d",
             tm_now.tm_mday, tm_now.tm_mon + 1, tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    lines[count++].is_header = false;

    snprintf(lines[count].text, sizeof(lines[count].text),
             "Sync: %s", Sys_Info.isTimeSync ? "Synced (NTP/PC)" : "Unsynced");
    lines[count++].is_header = false;

    esp_reset_reason_t rst_reason = esp_reset_reason();
    snprintf(lines[count].text, sizeof(lines[count].text),
             "Reset: %s", get_reset_reason_name(rst_reason));
    lines[count++].is_header = false;

    snprintf(lines[count].text, sizeof(lines[count].text),
             "Reset Count: %lu", (unsigned long)reset_count);
    lines[count++].is_header = false;

    /* ── Vẽ các dòng hiển thị trong cửa sổ cuộn ── */
    uint8_t visible = count - s_device_info_scroll_offset;
    if (visible > VISIBLE_ITEMS) visible = VISIBLE_ITEMS;

    for (uint8_t i = 0; i < visible; i++) {
        uint8_t idx = s_device_info_scroll_offset + i;
        uint8_t y = ITEMS_Y_START + i * ITEM_ROW_H;

        if (lines[idx].is_header) {
            LCD_FillRect(0, y, 128, ITEM_ROW_H, LCD_COLOR_ON);
            uint8_t w = strlen(lines[idx].text) * 6;
            uint8_t x = (128 > w) ? (128 - w) / 2 : 0;
            LCD_DrawString(x, y + 1, lines[idx].text, LCD_COLOR_OFF);
        } else {
            LCD_DrawStringScroll(4, y + 1, lines[idx].text, 120, 0, LCD_COLOR_ON);
        }
    }

    if (s_device_info_scroll_offset > 0) {
        draw_arrow_up(120, ITEMS_Y_START + 1, LCD_COLOR_ON);
    }
    if (s_device_info_scroll_offset + VISIBLE_ITEMS < count) {
        draw_arrow_down(120, STATUS_BAR_Y - 6, LCD_COLOR_ON);
    }

    LCD_FillRect(0, STATUS_BAR_Y, 128, STATUS_BAR_H, LCD_COLOR_ON);
    draw_arrow_down_large(8, STATUS_BAR_Y + 3, LCD_COLOR_OFF);
    draw_arrow_up_large(32, STATUS_BAR_Y + 3, LCD_COLOR_OFF);
    LCD_DrawString(80, STATUS_BAR_Y + 3, "ESC", LCD_COLOR_OFF);
    LCD_Flush();
}

static void menu_handle_device_info_buttons(void)
{
    const uint8_t total_lines = 19;
    if (btn_edge(BTN_IDX_ESC)) {
        s_device_info_scroll_offset = 0;
        goto_page(PAGE_MAIN_MENU);
        g_lcd_need_redraw = true;
        return;
    }

    if (btn_edge(BTN_IDX_UP)) {
        if (s_device_info_scroll_offset > 0) {
            s_device_info_scroll_offset--;
            g_lcd_need_redraw = true;
        }
    }

    if (btn_edge(BTN_IDX_DOWN)) {
        if (s_device_info_scroll_offset + VISIBLE_ITEMS < total_lines) {
            s_device_info_scroll_offset++;
            g_lcd_need_redraw = true;
        }
    }

    btn_edge(BTN_IDX_RIGHT);
    btn_edge(BTN_IDX_ENTER);
}

static void menu_handle_leaf_select(void)

{
    menu_page_t cur = g_menu.current_page;
    uint8_t sel = g_menu.selected;

    if (cur == PAGE_LANGUAGE) {
        g_sys_lang = (sys_lang_t)sel;
        Nvs_Write_Number("sys_lang", (uint32_t)g_sys_lang);
        ESP_LOGI(TAG_MENU, "Luu ngon ngu NVS: %d", g_sys_lang);
        menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Ngon Ngu" : "Language", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
        g_lcd_need_redraw = true;
        goto_page(PAGE_SYSTEM_SETTINGS);
    }
    else if (cur == PAGE_DATE_FORMAT) {
        g_date_format = (date_format_t)sel;
        Nvs_Write_Number("date_format", (uint32_t)g_date_format);
        ESP_LOGI(TAG_MENU, "Luu dinh dang ngay NVS: %d", g_date_format);
        menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Dinh Dang Ngay" : "Day Format", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
        g_lcd_need_redraw = true;
        goto_page(PAGE_DATE);
    }
    else if (cur == PAGE_DISPLAY_MODE) {
        g_display_mode = (display_mode_t)sel;
        Nvs_Write_Number("disp_mode", (uint32_t)g_display_mode);
        ESP_LOGI(TAG_MENU, "Luu che do hien thi NVS: %d", g_display_mode);
        menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Che Do Hien Thi" : "Display Mode", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
        g_lcd_need_redraw = true;
        goto_page(PAGE_MAIN_MENU);
    }
    else if (cur == PAGE_DIGITAL_FILTER) {
        g_filter_level = (filter_level_t)sel;
        Nvs_Write_Number("filter_lvl", (uint32_t)g_filter_level);
        update_system_filters_level(g_filter_level);
        ESP_LOGI(TAG_MENU, "Luu bo loc so NVS: %d", g_filter_level);
        menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Bo Loc So" : "Digital Filter", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
        g_lcd_need_redraw = true;
        goto_page(PAGE_PH_SETTINGS);
    }
    else if (cur == PAGE_TEMP_MODE) {
        g_temp_mode = (temp_mode_t)sel;
        Save_Temp_Settings_To_Storage();
        ESP_LOGI(TAG_MENU, "Luu che do nhiet do NVS: %d", g_temp_mode);
        menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Che Do Nhiet Do" : "Temp Mode", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
        g_lcd_need_redraw = true;
        goto_page(PAGE_PH_SETTINGS);
    }
    else if (cur == PAGE_CALIBRATION) {
        if (sel == 2) {
            s_confirm_reset_target = RESET_TARGET_PH;
            s_confirm_reset_prev_page = PAGE_CALIBRATION;
            goto_page(PAGE_CONFIRM_RESET);
            g_lcd_need_redraw = true;
        }
    }
    else if (cur == PAGE_DO_SETTINGS) {
        if (sel == 1) {
            s_confirm_reset_target = RESET_TARGET_DO;
            s_confirm_reset_prev_page = PAGE_DO_SETTINGS;
            goto_page(PAGE_CONFIRM_RESET);
            g_lcd_need_redraw = true;
        }
    }
    else if (cur == PAGE_CAL_2PT) {
        if (sel == 0) {
            s_cal_exec.target_ph = 4.00f;
            s_cal_exec.min_mv    = 140.0f;
            s_cal_exec.max_mv    = 210.0f;
            s_cal_exec.cal_type  = 2;
            s_cal_exec.group_idx = 0;
            goto_page(PAGE_CAL_EXEC);
        } else if (sel == 1) {
            s_cal_exec.target_ph = 7.00f;
            s_cal_exec.min_mv    = -30.0f;
            s_cal_exec.max_mv    = 30.0f;
            s_cal_exec.cal_type  = 2;
            s_cal_exec.group_idx = 0;
            goto_page(PAGE_CAL_EXEC);
        }
        g_lcd_need_redraw = true;
    }
    else if (cur == PAGE_CAL_3PT_G1) {
        if (sel == 0) {
            s_cal_exec.target_ph = 4.00f;
            s_cal_exec.min_mv    = 140.0f;
            s_cal_exec.max_mv    = 210.0f;
            s_cal_exec.cal_type  = 3;
            s_cal_exec.group_idx = 1;
            goto_page(PAGE_CAL_EXEC);
        } else if (sel == 1) {
            s_cal_exec.target_ph = 6.86f;
            s_cal_exec.min_mv    = -20.0f;
            s_cal_exec.max_mv    = 35.0f;
            s_cal_exec.cal_type  = 3;
            s_cal_exec.group_idx = 1;
            goto_page(PAGE_CAL_EXEC);
        } else if (sel == 2) {
            s_cal_exec.target_ph = 9.18f;
            s_cal_exec.min_mv    = -160.0f;
            s_cal_exec.max_mv    = -95.0f;
            s_cal_exec.cal_type  = 3;
            s_cal_exec.group_idx = 1;
            goto_page(PAGE_CAL_EXEC);
        }
        g_lcd_need_redraw = true;
    }
    else if (cur == PAGE_CAL_3PT_G2) {
        if (sel == 0) {
            s_cal_exec.target_ph = 4.00f;
            s_cal_exec.min_mv    = 140.0f;
            s_cal_exec.max_mv    = 210.0f;
            s_cal_exec.cal_type  = 3;
            s_cal_exec.group_idx = 2;
            goto_page(PAGE_CAL_EXEC);
        } else if (sel == 1) {
            s_cal_exec.target_ph = 7.00f;
            s_cal_exec.min_mv    = -30.0f;
            s_cal_exec.max_mv    = 30.0f;
            s_cal_exec.cal_type  = 3;
            s_cal_exec.group_idx = 2;
            goto_page(PAGE_CAL_EXEC);
        } else if (sel == 2) {
            s_cal_exec.target_ph = 10.00f;
            s_cal_exec.min_mv    = -210.0f;
            s_cal_exec.max_mv    = -140.0f;
            s_cal_exec.cal_type  = 3;
            s_cal_exec.group_idx = 2;
            goto_page(PAGE_CAL_EXEC);
        }
        g_lcd_need_redraw = true;
    }
    else if (cur == PAGE_RESET_SENSOR) {
        if (sel == 0) {
            s_confirm_reset_target = RESET_TARGET_PH;
            s_confirm_reset_prev_page = PAGE_RESET_SENSOR;
            goto_page(PAGE_CONFIRM_RESET);
        } else if (sel == 1) {
            s_confirm_reset_target = RESET_TARGET_DO;
            s_confirm_reset_prev_page = PAGE_RESET_SENSOR;
            goto_page(PAGE_CONFIRM_RESET);
        }
        g_lcd_need_redraw = true;
    }
    else if (cur == PAGE_CAL_DO) {
        if (sel == 3) {
            s_confirm_reset_target = RESET_TARGET_DO;
            s_confirm_reset_prev_page = PAGE_CAL_DO;
            goto_page(PAGE_CONFIRM_RESET);
            g_lcd_need_redraw = true;
        }
    }
    else if (cur == PAGE_MODBUS_SELECT_BAUD) {
        uint32_t bauds[] = {2400, 4800, 9600, 19200, 38400, 57600, 115200};
        uint32_t val = bauds[sel];
        if (s_modbus_edit_port == 1) {
            g_mb1_baud = val;
            Nvs_Write_Number("mb1_baud", val);
            ESP_LOGI(TAG_MENU, "Luu Baudrate Port 1: %lu", (unsigned long)val);
            menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Toc Do Baud" : "Baud Rate", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
            goto_page(PAGE_MODBUS_PORT1);
        } else {
            g_mb2_baud = val;
            Nvs_Write_Number("mb2_baud", val);
            do_sensor_update_config(g_mb2_addr, g_mb2_baud, g_mb2_parity, g_mb2_stop);
            ESP_LOGI(TAG_MENU, "Luu Baudrate Port 2: %lu", (unsigned long)val);
            menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Toc Do Baud" : "Baud Rate", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
            goto_page(PAGE_MODBUS_PORT2);
        }
        g_lcd_need_redraw = true;
    }
    else if (cur == PAGE_MODBUS_SELECT_PARITY) {
        if (s_modbus_edit_port == 1) {
            g_mb1_parity = sel;
            Nvs_Write_Number("mb1_parity", sel);
            ESP_LOGI(TAG_MENU, "Luu Parity Port 1: %d", sel);
            menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Kiem Tra Parity" : "Parity Check", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
            goto_page(PAGE_MODBUS_PORT1);
        } else {
            g_mb2_parity = sel;
            Nvs_Write_Number("mb2_parity", sel);
            do_sensor_update_config(g_mb2_addr, g_mb2_baud, g_mb2_parity, g_mb2_stop);
            ESP_LOGI(TAG_MENU, "Luu Parity Port 2: %d", sel);
            menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Kiem Tra Parity" : "Parity Check", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
            goto_page(PAGE_MODBUS_PORT2);
        }
        g_lcd_need_redraw = true;
    }
    else if (cur == PAGE_MODBUS_SELECT_STOP) {
        uint8_t stops[] = {1, 2};
        if (s_modbus_edit_port == 1) {
            g_mb1_stop = stops[sel];
            Nvs_Write_Number("mb1_stop", g_mb1_stop);
            ESP_LOGI(TAG_MENU, "Luu Stop Bits Port 1: %d", g_mb1_stop);
            menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Bit Stop" : "Stop Bits", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
            goto_page(PAGE_MODBUS_PORT1);
        } else {
            g_mb2_stop = stops[sel];
            Nvs_Write_Number("mb2_stop", g_mb2_stop);
            do_sensor_update_config(g_mb2_addr, g_mb2_baud, g_mb2_parity, g_mb2_stop);
            ESP_LOGI(TAG_MENU, "Luu Stop Bits Port 2: %d", g_mb2_stop);
            menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Bit Stop" : "Stop Bits", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
            goto_page(PAGE_MODBUS_PORT2);
        }
        g_lcd_need_redraw = true;
    }
}



static void menu_render_modbus_addr(void)
{
    LCD_Clear();

    // 1. Tiêu đề động tùy thuộc vào cổng đang chỉnh sửa
    LCD_FillRect(0, 0, 128, TITLE_BAR_H, LCD_COLOR_ON);
    char title_buf[32];
    if (g_sys_lang == LANG_VI) {
        snprintf(title_buf, sizeof(title_buf), "Dia Chi Cong %d", s_modbus_edit_port);
    } else {
        snprintf(title_buf, sizeof(title_buf), "Port %d Address", s_modbus_edit_port);
    }
    LCD_DrawString(4, 1, title_buf, LCD_COLOR_OFF);

    // 2. Thông tin khoảng giới hạn địa chỉ cho phép
    char info_str[32];
    snprintf(info_str, sizeof(info_str), g_sys_lang == LANG_VI ? "Gia tri: 1 - 247" : "Range: 1 - 247");
    uint8_t info_w = strlen(info_str) * 6;
    uint8_t info_x = (LCD_WIDTH - info_w) / 2;
    LCD_DrawString(info_x, 12, info_str, LCD_COLOR_ON);

    // 3. Hiển thị giá trị địa chỉ đang sửa với kích thước 2x2 kèm gạch chân lựa chọn
    char val_str[16];
    snprintf(val_str, sizeof(val_str), "%d", s_modbus_addr_edit);
    uint8_t val_w = strlen(val_str) * 6 * 2;
    uint8_t val_x = (LCD_WIDTH - val_w) / 2;
    LCD_DrawStringScaled(val_x, 22, val_str, 2, 2, LCD_COLOR_ON);

    LCD_DrawHLine(val_x, 39, val_w - 6, LCD_COLOR_ON);
    LCD_DrawHLine(val_x, 40, val_w - 6, LCD_COLOR_ON);

    // 4. Thanh trạng thái các nút bấm dưới cùng
    LCD_FillRect(0, STATUS_BAR_Y, 128, STATUS_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(8, STATUS_BAR_Y + 3, g_sys_lang == LANG_VI ? "HUY" : "ESC", LCD_COLOR_OFF);
    LCD_DrawString(44, STATUS_BAR_Y + 3, "-/+", LCD_COLOR_OFF);
    LCD_DrawString(100, STATUS_BAR_Y + 3, g_sys_lang == LANG_VI ? "LUU" : "ENT", LCD_COLOR_OFF);

    LCD_Flush();
}

static void menu_handle_modbus_addr_buttons(void)
{
    if (btn_edge(BTN_IDX_ESC)) {
        goto_page(s_modbus_edit_port == 1 ? PAGE_MODBUS_PORT1 : PAGE_MODBUS_PORT2);
        g_lcd_need_redraw = true;
        return;
    }

    if (btn_edge(BTN_IDX_UP)) {
        if (s_modbus_addr_edit < 247) {
            s_modbus_addr_edit++;
        } else {
            s_modbus_addr_edit = 1;
        }
        g_lcd_need_redraw = true;
    }

    if (btn_edge(BTN_IDX_DOWN)) {
        if (s_modbus_addr_edit > 1) {
            s_modbus_addr_edit--;
        } else {
            s_modbus_addr_edit = 247;
        }
        g_lcd_need_redraw = true;
    }

    if (btn_edge(BTN_IDX_ENTER)) {
        if (s_modbus_edit_port == 1) {
            g_mb1_addr = s_modbus_addr_edit;
            Nvs_Write_Number("mb1_addr", g_mb1_addr); // Lưu NVS flash
            ESP_LOGI("MENU", "Luu Dia Chi Cong 1: %d", g_mb1_addr);
        } else {
            g_mb2_addr = s_modbus_addr_edit;
            Nvs_Write_Number("mb2_addr", g_mb2_addr); // Lưu NVS flash
            ESP_LOGI("MENU", "Luu Dia Chi Cong 2: %d", g_mb2_addr);
            
            // Cập nhật cấu hình UART Modbus cho cảm biến DO ngay lập tức
            do_sensor_update_config(g_mb2_addr, g_mb2_baud, g_mb2_parity, g_mb2_stop);
        }

        // Hiện popup thông báo thành công
        if (g_sys_lang == LANG_VI) {
            menu_show_alert_dialog("Dia Chi MB", "Thanh Cong!", true);
        } else {
            menu_show_alert_dialog("MB Address", "SUCCESS!", true);
        }

        goto_page(s_modbus_edit_port == 1 ? PAGE_MODBUS_PORT1 : PAGE_MODBUS_PORT2);
        g_lcd_need_redraw = true;
    }

    // RIGHT – dự phòng (bỏ qua)
    btn_edge(BTN_IDX_RIGHT);
}

static void menu_render_cal_exec(void)
{
    LCD_Clear();

    // 1. Tiêu đề
    LCD_FillRect(0, 0, 128, TITLE_BAR_H, LCD_COLOR_ON);
    char title_buf[32];
    snprintf(title_buf, sizeof(title_buf), g_sys_lang == LANG_VI ? "Hieu Chuan %.2f pH" : "Calibrate %.2f pH", s_cal_exec.target_ph);
    LCD_DrawString(4, 1, title_buf, LCD_COLOR_OFF);

    // 2. Mốc target pH ở giữa màn hình (Scale 2x2)
    char target_str[16];
    snprintf(target_str, sizeof(target_str), "%.2f pH", s_cal_exec.target_ph);
    uint8_t w = strlen(target_str) * 6 * 2;
    uint8_t x = (LCD_WIDTH - w) / 2;
    LCD_DrawStringScaled(x, 16, target_str, 2, 2, LCD_COLOR_ON);

    // 3. Thông số điện áp mV và Nhiệt độ đo thời gian thực
    PH_Temp_Sensor_Status_t status = Get_Sensor_Status();
    bool is_valid = (status.v_probe_mv >= s_cal_exec.min_mv && status.v_probe_mv <= s_cal_exec.max_mv);

    char temp_buf[16];
    char mv_buf[32];
    snprintf(temp_buf, sizeof(temp_buf), "%.1f\xB0" "C", status.temperature);
    snprintf(mv_buf, sizeof(mv_buf), "%.1f mV", status.v_probe_mv);

    LCD_DrawString(4, 38, temp_buf, LCD_COLOR_ON);
    LCD_DrawString(48, 38, is_valid ? "OK" : "ERR", LCD_COLOR_ON);

    uint8_t mv_w = strlen(mv_buf) * 6;
    uint8_t mv_x = (LCD_WIDTH - 4 > mv_w) ? (LCD_WIDTH - 4 - mv_w) : 70;
    LCD_DrawString(mv_x, 38, mv_buf, LCD_COLOR_ON);

    // 4. Thanh nút bấm phía dưới (Nút LUU chỉ xuất hiện khi điện áp hợp lệ OK)
    LCD_FillRect(0, STATUS_BAR_Y, 128, STATUS_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(8, STATUS_BAR_Y + 3, g_sys_lang == LANG_VI ? "HUY" : "ESC", LCD_COLOR_OFF);
    if (is_valid) {
        LCD_DrawString(100, STATUS_BAR_Y + 3, g_sys_lang == LANG_VI ? "LUU" : "ENT", LCD_COLOR_OFF);
    }

    LCD_Flush();
}

static void menu_handle_cal_exec_buttons(void)
{
    if (btn_edge(BTN_IDX_ESC)) {
        if (s_cal_exec.cal_type == 2) {
            goto_page(PAGE_CAL_2PT);
        } else if (s_cal_exec.group_idx == 1) {
            goto_page(PAGE_CAL_3PT_G1);
        } else {
            goto_page(PAGE_CAL_3PT_G2);
        }
        g_lcd_need_redraw = true;
        return;
    }

    if (btn_edge(BTN_IDX_ENTER)) {
        PH_Temp_Sensor_Status_t status = Get_Sensor_Status();
        bool is_valid = (status.v_probe_mv >= s_cal_exec.min_mv && status.v_probe_mv <= s_cal_exec.max_mv);
        if (is_valid) {
            bool ok = Calibrate_PH_Point(s_cal_exec.target_ph, status.v_probe_mv, status.temperature, s_cal_exec.cal_type);
            if (ok) {
                menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Hieu Chuan pH" : "pH Cal", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
            } else {
                menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Hieu Chuan pH" : "pH Cal", g_sys_lang == LANG_VI ? "That Bai!" : "FAILED!", false);
            }

            if (s_cal_exec.cal_type == 2) {
                goto_page(PAGE_CAL_2PT);
            } else if (s_cal_exec.group_idx == 1) {
                goto_page(PAGE_CAL_3PT_G1);
            } else {
                goto_page(PAGE_CAL_3PT_G2);
            }
            g_lcd_need_redraw = true;
        }
    }

    // RIGHT – dự phòng (bỏ qua)
    btn_edge(BTN_IDX_RIGHT);
}

static void menu_render_cal_do_exec(void)
{
    LCD_Clear();

    // 1. Tiêu đề động tùy thuộc loại hiệu chuẩn
    LCD_FillRect(0, 0, 128, TITLE_BAR_H, LCD_COLOR_ON);
    char title_buf[32];
    if (s_do_cal_exec.do_cal_type == 0) {
        snprintf(title_buf, sizeof(title_buf), g_sys_lang == LANG_VI ? "Hieu Chuan DO Diem 0" : "Cal. DO Zero");
    } else {
        snprintf(title_buf, sizeof(title_buf), g_sys_lang == LANG_VI ? "Hieu Chuan DO Do Doc" : "Cal. DO Slope");
    }
    LCD_DrawString(4, 1, title_buf, LCD_COLOR_OFF);

    // 2. Hiển thị thông số nồng độ DO hiện tại dạng Scaled chữ lớn (2x2)
    PH_Temp_Sensor_Status_t status = Get_Sensor_Status();
    char val_str[32];
    snprintf(val_str, sizeof(val_str), "%.2f mg/L", status.do_mg_l);
    
    uint8_t w = strlen(val_str) * 6 * 2; 
    uint8_t x = (LCD_WIDTH - w) / 2;
    LCD_DrawStringScaled(x, 16, val_str, 2, 2, LCD_COLOR_ON);

    // 3. Hiển thị nhiệt độ hiện tại (T) và Độ bão hòa (Sat)
    char extra_str[64];
    snprintf(extra_str, sizeof(extra_str), "T:%.1f\xB0" "C  Sat:%.1f%%", status.do_temp_c, status.do_saturation_pct);
    uint8_t ext_w = strlen(extra_str) * 6;
    uint8_t ext_x = (LCD_WIDTH - ext_w) / 2;
    LCD_DrawString(ext_x, 36, extra_str, LCD_COLOR_ON);

    // 4. Thanh nút bấm phía dưới
    LCD_FillRect(0, STATUS_BAR_Y, 128, STATUS_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(8, STATUS_BAR_Y + 3, g_sys_lang == LANG_VI ? "HUY" : "ESC", LCD_COLOR_OFF);
    LCD_DrawString(100, STATUS_BAR_Y + 3, g_sys_lang == LANG_VI ? "LUU" : "ENT", LCD_COLOR_OFF);
    
    LCD_Flush();
}

static void menu_handle_cal_do_exec_buttons(void)
{
    if (btn_edge(BTN_IDX_ESC)) {
        goto_page(PAGE_CAL_DO);
        g_lcd_need_redraw = true;
        return;
    }

    if (btn_edge(BTN_IDX_ENTER)) {
        esp_err_t err;
        if (s_do_cal_exec.do_cal_type == 0) {
            err = do_sensor_calibrate_zero(); // Hiệu chuẩn điểm 0
            if (err == ESP_OK) {
                menu_show_alert_dialog("Hieu Chuan DO", g_sys_lang == LANG_VI ? "Diem 0: OK!" : "Zero: Success!", true);
            } else {
                menu_show_alert_dialog("Hieu Chuan DO", g_sys_lang == LANG_VI ? "Diem 0: That Bai" : "Zero: Failed", false);
            }
        } else {
            err = do_sensor_calibrate_slope(); // Hiệu chuẩn độ dốc
            if (err == ESP_OK) {
                menu_show_alert_dialog("Hieu Chuan DO", g_sys_lang == LANG_VI ? "Do Doc: OK!" : "Slope: Success!", true);
            } else {
                menu_show_alert_dialog("Hieu Chuan DO", g_sys_lang == LANG_VI ? "Do Doc: That Bai" : "Slope: Failed", false);
            }
        }
        goto_page(PAGE_CAL_DO);
        g_lcd_need_redraw = true;
    }

    // RIGHT – dự phòng (bỏ qua)
    btn_edge(BTN_IDX_RIGHT);
}

static void menu_render_cal_do_temp(void)
{
    LCD_Clear();

    // 1. Tiêu đề
    LCD_FillRect(0, 0, 128, TITLE_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(4, 1, g_sys_lang == LANG_VI ? "Hieu Chinh Nhiet Do DO" : "Cal. DO Temp", LCD_COLOR_OFF);

    // 2. Chỉ dẫn
    char info_str[32];
    snprintf(info_str, sizeof(info_str), g_sys_lang == LANG_VI ? "Nhap nhiet do chuan" : "Enter standard temp");
    uint8_t info_w = strlen(info_str) * 6;
    uint8_t info_x = (LCD_WIDTH - info_w) / 2;
    LCD_DrawString(info_x, 12, info_str, LCD_COLOR_ON);

    // 3. Giá trị lớn đang chỉnh sửa
    char val_str[16];
    snprintf(val_str, sizeof(val_str), "%.1f\xB0" "C", s_do_temp_cal_edit);
    uint8_t val_w = strlen(val_str) * 6 * 2;
    uint8_t val_x = (LCD_WIDTH - val_w) / 2;
    LCD_DrawStringScaled(val_x, 22, val_str, 2, 2, LCD_COLOR_ON);

    LCD_DrawHLine(val_x, 39, val_w - 6, LCD_COLOR_ON);
    LCD_DrawHLine(val_x, 40, val_w - 6, LCD_COLOR_ON);

    // 4. Kết quả nhiệt độ hiện tại từ cảm biến
    char res_str[48];
    PH_Temp_Sensor_Status_t status = Get_Sensor_Status();
    snprintf(res_str, sizeof(res_str), g_sys_lang == LANG_VI ? "Hien tai: %.1f\xB0" "C" : "Current: %.1f\xB0" "C", status.do_temp_c);
    uint8_t res_w = strlen(res_str) * 6;
    uint8_t res_x = (LCD_WIDTH - res_w) / 2;
    LCD_DrawString(res_x, 43, res_str, LCD_COLOR_ON);

    // 5. Thanh nút bấm phía dưới
    LCD_FillRect(0, STATUS_BAR_Y, 128, STATUS_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(8, STATUS_BAR_Y + 3, g_sys_lang == LANG_VI ? "HUY" : "ESC", LCD_COLOR_OFF);
    LCD_DrawString(44, STATUS_BAR_Y + 3, "-/+", LCD_COLOR_OFF);
    LCD_DrawString(100, STATUS_BAR_Y + 3, g_sys_lang == LANG_VI ? "LUU" : "ENT", LCD_COLOR_OFF);

    LCD_Flush();
}

static void menu_handle_cal_do_temp_buttons(void)
{
    if (btn_edge(BTN_IDX_ESC)) {
        goto_page(PAGE_CAL_DO);
        g_lcd_need_redraw = true;
        return;
    }

    if (btn_edge(BTN_IDX_UP)) {
        if (s_do_temp_cal_edit < 99.95f) {
            s_do_temp_cal_edit += 0.1f;
        }
        g_lcd_need_redraw = true;
    }

    if (btn_edge(BTN_IDX_DOWN)) {
        if (s_do_temp_cal_edit > 0.05f) {
            s_do_temp_cal_edit -= 0.1f;
        }
        g_lcd_need_redraw = true;
    }

    if (btn_edge(BTN_IDX_ENTER)) {
        esp_err_t err = do_sensor_correct_temp(s_do_temp_cal_edit);
        if (err == ESP_OK) {
            menu_show_alert_dialog("Nhiet Do DO", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
        } else {
            menu_show_alert_dialog("Nhiet Do DO", g_sys_lang == LANG_VI ? "That Bai!" : "FAILED!", false);
        }
        goto_page(PAGE_CAL_DO);
        g_lcd_need_redraw = true;
    }

    // RIGHT – dự phòng (bỏ qua)
    btn_edge(BTN_IDX_RIGHT);
}

/* ── Màn hình Hỏi xác nhận Reset cảm biến (pH / DO) ── */
static void menu_render_confirm_reset(void)
{
    LCD_Clear();

    // 1. Tiêu đề
    LCD_FillRect(0, 0, 128, TITLE_BAR_H, LCD_COLOR_ON);
    const char *title = (g_sys_lang == LANG_VI) ? "Xac Nhan Reset" : "Confirm Reset";
    LCD_DrawString(4, 1, title, LCD_COLOR_OFF);

    // 2. Nội dung câu hỏi và chi tiết mục reset
    const char *line1 = (g_sys_lang == LANG_VI) ? "Ban co chac chan?" : "Are you sure?";
    const char *line2 = (s_confirm_reset_target == RESET_TARGET_PH) ?
                        ((g_sys_lang == LANG_VI) ? "Reset sensor pH?" : "Reset pH sensor?") :
                        ((g_sys_lang == LANG_VI) ? "Reset sensor DO?" : "Reset DO sensor?");
    const char *line3 = (g_sys_lang == LANG_VI) ? "Dua ve mac dinh!" : "Reset to default!";

    uint8_t w1 = strlen(line1) * 6;
    uint8_t w2 = strlen(line2) * 6;
    uint8_t w3 = strlen(line3) * 6;

    LCD_DrawString((LCD_WIDTH - w1) / 2, 16, line1, LCD_COLOR_ON);
    LCD_DrawString((LCD_WIDTH - w2) / 2, 27, line2, LCD_COLOR_ON);
    LCD_DrawString((LCD_WIDTH - w3) / 2, 38, line3, LCD_COLOR_ON);

    // 3. Thanh nút bấm phía dưới (ESC: HUY, ENT: RESET)
    LCD_FillRect(0, STATUS_BAR_Y, 128, STATUS_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(8, STATUS_BAR_Y + 3, (g_sys_lang == LANG_VI) ? "HUY" : "ESC", LCD_COLOR_OFF);
    LCD_DrawString(84, STATUS_BAR_Y + 3, (g_sys_lang == LANG_VI) ? "RESET" : "ENT", LCD_COLOR_OFF);

    LCD_Flush();
}

static void menu_handle_confirm_reset_buttons(void)
{
    if (btn_edge(BTN_IDX_ESC)) {
        goto_page(s_confirm_reset_prev_page);
        g_lcd_need_redraw = true;
        return;
    }

    if (btn_edge(BTN_IDX_ENTER)) {
        if (s_confirm_reset_target == RESET_TARGET_PH) {
            Reset_PH_Calibration();
            menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Khoi Phuc pH" : "Reset pH", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
        } else {
            esp_err_t err = do_sensor_reset();
            if (err == ESP_OK) {
                menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Khoi Phuc DO" : "Reset DO", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
            } else {
                menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Khoi Phuc DO" : "Reset DO", g_sys_lang == LANG_VI ? "That Bai!" : "FAILED!", false);
            }
        }
        goto_page(s_confirm_reset_prev_page);
        g_lcd_need_redraw = true;
        return;
    }

    btn_edge(BTN_IDX_RIGHT);
}

/* ── Màn hình Kiểm tra Sức khỏe Cảm biến pH ── */
static void menu_render_ph_health(void)
{
    LCD_Clear();

    // 1. Tiêu đề (nền đen chữ trắng)
    LCD_FillRect(0, 0, 128, TITLE_BAR_H, LCD_COLOR_ON);
    const char *title = (g_sys_lang == LANG_VI) ? "Suc Khoe Cam Bien pH" : "pH Sensor Health";
    LCD_DrawString(4, 1, title, LCD_COLOR_OFF);

    // 2. Lấy dữ liệu chẩn đoán sức khỏe cảm biến pH
    PhSensorHealth_t health = Get_PH_Sensor_Health();

    if (!health.is_calibrated) {
        // Chưa được hiệu chuẩn thực tế
        const char *msg1 = (g_sys_lang == LANG_VI) ? "Chua Hieu Chuan!" : "Not Calibrated!";
        const char *msg2 = (g_sys_lang == LANG_VI) ? "Vui long calib ph" : "Please calib pH";
        const char *msg3 = (g_sys_lang == LANG_VI) ? "de tinh suc khoe." : "to get health data";

        LCD_DrawString((128 - strlen(msg1) * 6) / 2, 16, msg1, LCD_COLOR_ON);
        LCD_DrawString((128 - strlen(msg2) * 6) / 2, 28, msg2, LCD_COLOR_ON);
        LCD_DrawString((128 - strlen(msg3) * 6) / 2, 38, msg3, LCD_COLOR_ON);
    } else {
        // Dòng 1: Slope % & sensitivity
        char slope_str[32];
        snprintf(slope_str, sizeof(slope_str), "Slope : %.1f%% (%.1fmV)", health.slope_pct, health.sens_mv_per_ph);
        LCD_DrawString(2, 13, slope_str, LCD_COLOR_ON);

        // Dòng 2: Zero Offset mV
        char zero_str[32];
        snprintf(zero_str, sizeof(zero_str), "Zero  : %s%.1f mV", (health.zero_offset_mv >= 0.0f) ? "+" : "", health.zero_offset_mv);
        LCD_DrawString(2, 23, zero_str, LCD_COLOR_ON);

        // Dòng 3 & 4: Đánh giá trạng thái (Status / Cảnh báo)
        if (health.is_healthy) {
            // Cảm biến hoạt động tốt (Good / Healthy)
            const char *status_str = (g_sys_lang == LANG_VI) ? "Danh gia: TOT (OK)" : "Status  : GOOD (OK)";
            LCD_DrawString(2, 34, status_str, LCD_COLOR_ON);

            const char *sub_str = (g_sys_lang == LANG_VI) ? "Dien cuc hoat dong tot" : "Sensor working well";
            LCD_DrawString(2, 43, sub_str, LCD_COLOR_ON);
        } else {
            // Cảnh báo cảm biến già / cần vệ sinh điện cực
            LCD_FillRect(0, 33, 128, 17, LCD_COLOR_ON); // Banner đen nổi bật

            const char *warn1 = (g_sys_lang == LANG_VI) ? "CAN VE SINH DIEN CUC!" : "CLEAN SENSOR / AGING!";
            const char *warn2 = (g_sys_lang == LANG_VI) ? "Slope <80% hoac |Z|>30mV" : "Slope <80% or |Z|>30mV";

            uint8_t w1 = strlen(warn1) * 6;
            uint8_t w2 = strlen(warn2) * 6;
            LCD_DrawString((128 - w1) / 2, 35, warn1, LCD_COLOR_OFF);
            LCD_DrawString((128 - w2) / 2, 43, warn2, LCD_COLOR_OFF);
        }
    }

    // 3. Status bar dưới cùng (ESC)
    LCD_FillRect(0, STATUS_BAR_Y, 128, STATUS_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(8, STATUS_BAR_Y + 3, (g_sys_lang == LANG_VI) ? "HUY" : "ESC", LCD_COLOR_OFF);
    LCD_DrawString(100, STATUS_BAR_Y + 3, (g_sys_lang == LANG_VI) ? "THOAT" : "BACK", LCD_COLOR_OFF);

    LCD_Flush();
}

static void menu_handle_ph_health_buttons(void)
{
    if (btn_edge(BTN_IDX_ESC) || btn_edge(BTN_IDX_ENTER)) {
        goto_page(PAGE_PH_SETTINGS);
        g_lcd_need_redraw = true;
        return;
    }

    btn_edge(BTN_IDX_RIGHT);
    btn_edge(BTN_IDX_UP);
    btn_edge(BTN_IDX_DOWN);
}

static void menu_render_temp_settings(void)
{
    LCD_Clear();

    bool is_f = (g_temp_mode == TEMP_MODE_ATC_F || g_temp_mode == TEMP_MODE_MTC_F);
    bool is_mtc = (g_temp_mode == TEMP_MODE_MTC_C || g_temp_mode == TEMP_MODE_MTC_F);

    // 1. Tiêu đề
    LCD_FillRect(0, 0, 128, TITLE_BAR_H, LCD_COLOR_ON);
    char title_buf[32];
    if (is_mtc) {
        snprintf(title_buf, sizeof(title_buf), g_sys_lang == LANG_VI ? "Nhiet Do Thu Cong" : "Manual Temp");
    } else {
        snprintf(title_buf, sizeof(title_buf), g_sys_lang == LANG_VI ? "Hieu Chinh Nhiet Do" : "Temp Calibration");
    }
    LCD_DrawString(4, 1, title_buf, LCD_COLOR_OFF);

    // 2. Đơn vị hiển thị phụ thuộc chế độ
    char info_str[32];
    snprintf(info_str, sizeof(info_str), "Unit: \xB0" "%s (%s)", is_f ? "F" : "C", is_mtc ? "MTC" : "ATC");
    LCD_DrawString(24, 12, info_str, LCD_COLOR_ON);

    // 3. Hiển thị giá trị lớn đang chỉnh sửa
    char val_str[32];
    if (is_mtc) {
        snprintf(val_str, sizeof(val_str), "%.1f\xB0" "%s", s_manual_temp_edit, is_f ? "F" : "C");
    } else {
        snprintf(val_str, sizeof(val_str), "%s%.1f\xB0" "%s", (s_temp_offset_edit >= 0.0f) ? "+" : "", s_temp_offset_edit, is_f ? "F" : "C");
    }
    uint8_t val_w = strlen(val_str) * 6 * 2;
    uint8_t val_x = (LCD_WIDTH - val_w) / 2;
    LCD_DrawStringScaled(val_x, 22, val_str, 2, 2, LCD_COLOR_ON);
    
    // Gạch chân biểu thị đang chọn
    LCD_DrawHLine(val_x, 39, val_w - 6, LCD_COLOR_ON);
    LCD_DrawHLine(val_x, 40, val_w - 6, LCD_COLOR_ON);

    // 4. Kết quả nhiệt độ thực tế cuối cùng sau chỉnh sửa
    char res_str[48];
    PH_Temp_Sensor_Status_t status = Get_Sensor_Status();
    float final_temp = 25.0f;
    if (is_mtc) {
        final_temp = s_manual_temp_edit;
    } else {
        float raw_temp_c = status.temperature - g_temp_offset; // Khôi phục nhiệt độ thô
        if (is_f) {
            float offset_c = s_temp_offset_edit / 1.8f;
            final_temp = (raw_temp_c + offset_c) * 1.8f + 32.0f;
        } else {
            final_temp = raw_temp_c + s_temp_offset_edit;
        }
    }
    snprintf(res_str, sizeof(res_str), g_sys_lang == LANG_VI ? "Nhiet do: %.1f\xB0" "%s" : "Result: %.1f\xB0" "%s", final_temp, is_f ? "F" : "C");
    LCD_DrawString(12, 43, res_str, LCD_COLOR_ON);

    // 5. Thanh nút bấm bên dưới
    LCD_FillRect(0, STATUS_BAR_Y, 128, STATUS_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(8, STATUS_BAR_Y + 3, "ESC", LCD_COLOR_OFF);
    LCD_DrawString(44, STATUS_BAR_Y + 3, "-/+", LCD_COLOR_OFF);
    LCD_DrawString(100, STATUS_BAR_Y + 3, "ENT", LCD_COLOR_OFF);
    
    LCD_Flush();
}

static void menu_handle_temp_settings_buttons(void)
{
    bool is_f = (g_temp_mode == TEMP_MODE_ATC_F || g_temp_mode == TEMP_MODE_MTC_F);
    bool is_mtc = (g_temp_mode == TEMP_MODE_MTC_C || g_temp_mode == TEMP_MODE_MTC_F);

    if (btn_edge(BTN_IDX_ESC)) {
        goto_page(PAGE_PH_SETTINGS);
        g_lcd_need_redraw = true;
        return;
    }

    // Tăng giá trị khi nhấn UP
    if (btn_edge(BTN_IDX_UP)) {
        if (is_mtc) {
            float max_limit = is_f ? 212.0f : 100.0f;
            if (s_manual_temp_edit < max_limit - 0.05f) s_manual_temp_edit += 0.1f;
        } else {
            float max_limit = is_f ? 18.0f : 10.0f; // Max offset: 10 C (18 F)
            if (s_temp_offset_edit < max_limit - 0.05f) s_temp_offset_edit += 0.1f;
        }
        g_lcd_need_redraw = true;
    }

    // Giảm giá trị khi nhấn DOWN
    if (btn_edge(BTN_IDX_DOWN)) {
        if (is_mtc) {
            float min_limit = is_f ? 32.0f : 0.0f;
            if (s_manual_temp_edit > min_limit + 0.05f) s_manual_temp_edit -= 0.1f;
        } else {
            float min_limit = is_f ? -18.0f : -10.0f; // Min offset: -10 C (-18 F)
            if (s_temp_offset_edit > min_limit + 0.05f) s_temp_offset_edit -= 0.1f;
        }
        g_lcd_need_redraw = true;
    }

    // Lưu cấu hình khi nhấn ENTER
    if (btn_edge(BTN_IDX_ENTER)) {
        if (is_mtc) {
            if (is_f) {
                g_manual_temp = (s_manual_temp_edit - 32.0f) / 1.8f; // Quy đổi F -> C để lưu trữ thống nhất
            } else {
                g_manual_temp = s_manual_temp_edit;
            }
            if (g_manual_temp < 0.0f) g_manual_temp = 0.0f;
            if (g_manual_temp > 100.0f) g_manual_temp = 100.0f;

            Save_Temp_Settings_To_Storage(); // Lưu vào NVS Flash
            menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Nhiet Do Thu Cong" : "Manual Temp", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
        } else {
            if (is_f) {
                g_temp_offset = s_temp_offset_edit / 1.8f;          // Quy đổi delta_F -> delta_C để lưu trữ
            } else {
                g_temp_offset = s_temp_offset_edit;
            }
            if (g_temp_offset < -10.0f) g_temp_offset = -10.0f;
            if (g_temp_offset > 10.0f) g_temp_offset = 10.0f;

            Save_Temp_Settings_To_Storage(); // Lưu vào NVS Flash
            menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Hieu Chinh T" : "Temp Calibration", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
        }
        goto_page(PAGE_PH_SETTINGS);
        g_lcd_need_redraw = true;
    }

    // RIGHT – dự phòng (bỏ qua)
    btn_edge(BTN_IDX_RIGHT);
}

static void menu_render_temp_lin_comp(void)
{
    LCD_Clear();

    // 1. Tiêu đề
    LCD_FillRect(0, 0, 128, TITLE_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(4, 1, g_sys_lang == LANG_VI ? "Bu Tuyen Tinh T" : "Temp Lin COMP", LCD_COLOR_OFF);

    // 2. Dòng thông tin nhãn hệ số alpha
    char info_str[32];
    snprintf(info_str, sizeof(info_str), g_sys_lang == LANG_VI ? "He so alpha (%%/\xB0" "C)" : "Alpha coeff (%%/\xB0" "C)");
    uint8_t info_w = strlen(info_str) * 6;
    uint8_t info_x = (LCD_WIDTH - info_w) / 2;
    LCD_DrawString(info_x, 12, info_str, LCD_COLOR_ON);

    // 3. Hiển thị giá trị hệ số alpha (%/C) dạng Scaled chữ lớn (2x2, y=22)
    char val_str[32];
    snprintf(val_str, sizeof(val_str), "%+.2f %%", s_temp_alpha_edit * 100.0f);
    uint8_t val_w = strlen(val_str) * 6 * 2;
    uint8_t val_x = (LCD_WIDTH - val_w) / 2;
    LCD_DrawStringScaled(val_x, 22, val_str, 2, 2, LCD_COLOR_ON);

    // Gạch chân biểu thị đang chọn
    LCD_DrawHLine(val_x, 39, val_w - 6, LCD_COLOR_ON);
    LCD_DrawHLine(val_x, 40, val_w - 6, LCD_COLOR_ON);

    // 4. Màn hình tính toán và hiển thị ngay giá trị pH đã qua hệ số alpha mới (y=43)
    char res_str[48];
    PH_Temp_Sensor_Status_t status = Get_Sensor_Status();
    float t_diff = status.temperature - 25.0f;
    // Khôi phục pH thô ban đầu (chưa bù alpha)
    float ph_raw = status.ph * (1.0f + g_temp_alpha * t_diff);
    // Tính toán lại pH đã bù theo hệ số alpha mới đang hiệu chỉnh
    float final_ph = ph_raw / (1.0f + s_temp_alpha_edit * t_diff);
    if (final_ph < 0.0f) final_ph = 0.0f;
    if (final_ph > 14.0f) final_ph = 14.0f;

    snprintf(res_str, sizeof(res_str), "pH (25\xB0" "C): %.2f pH", final_ph);
    uint8_t res_w = strlen(res_str) * 6;
    uint8_t res_x = (LCD_WIDTH - res_w) / 2;
    LCD_DrawString(res_x, 43, res_str, LCD_COLOR_ON);

    // 5. Thanh nút bấm phía dưới
    LCD_FillRect(0, STATUS_BAR_Y, 128, STATUS_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(8, STATUS_BAR_Y + 3, g_sys_lang == LANG_VI ? "HUY" : "ESC", LCD_COLOR_OFF);
    LCD_DrawString(44, STATUS_BAR_Y + 3, "-/+", LCD_COLOR_OFF);
    LCD_DrawString(100, STATUS_BAR_Y + 3, g_sys_lang == LANG_VI ? "LUU" : "ENT", LCD_COLOR_OFF);

    LCD_Flush();
}

static void menu_handle_temp_lin_comp_buttons(void)
{
    if (btn_edge(BTN_IDX_ESC)) {
        goto_page(PAGE_PH_SETTINGS);
        g_lcd_need_redraw = true;
        return;
    }

    // UP - Tăng giá trị hệ số alpha
    if (btn_edge(BTN_IDX_UP)) {
        if (s_temp_alpha_edit < 0.100f - 0.0005f) {
            s_temp_alpha_edit += 0.001f;
        }
        g_lcd_need_redraw = true;
    }

    // DOWN - Giảm giá trị hệ số alpha
    if (btn_edge(BTN_IDX_DOWN)) {
        if (s_temp_alpha_edit > -0.100f + 0.0005f) {
            s_temp_alpha_edit -= 0.001f;
        }
        g_lcd_need_redraw = true;
    }

    // ENTER - Lưu giá trị hệ số alpha
    if (btn_edge(BTN_IDX_ENTER)) {
        g_temp_alpha = s_temp_alpha_edit;
        if (g_temp_alpha < -0.100f) g_temp_alpha = -0.100f;
        if (g_temp_alpha > 0.100f) g_temp_alpha = 0.100f;

        Save_Temp_Settings_To_Storage();
        ESP_LOGI(TAG_MENU, "Da luu temp_alpha: %.3f", g_temp_alpha);
        if (g_sys_lang == LANG_VI) {
            menu_show_alert_dialog("He So Alpha", "Thanh Cong!", true);
        } else {
            menu_show_alert_dialog("Alpha Coeff", "SUCCESS!", true);
        }
        goto_page(PAGE_PH_SETTINGS);
        g_lcd_need_redraw = true;
    }

    // RIGHT – dự phòng (bỏ qua)
    btn_edge(BTN_IDX_RIGHT);
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

    /* Xử lý phím riêng cho màn hình Cài đặt nhiệt độ */
    if (g_menu.current_page == PAGE_TEMP_SETTINGS) {
        menu_handle_temp_settings_buttons();
        return;
    }

    /* Xử lý phím riêng cho màn hình Bù tuyến tính nhiệt độ */
    if (g_menu.current_page == PAGE_TEMP_LIN_COMP) {
        menu_handle_temp_lin_comp_buttons();
        return;
    }

    /* Xử lý phím riêng cho màn hình cấu hình Modbus Address */
    if (g_menu.current_page == PAGE_MODBUS_EDIT_ADDR) {
        menu_handle_modbus_addr_buttons();
        return;
    }

    /* Xử lý phím riêng cho màn hình thực thi hiệu chuẩn pH */
    if (g_menu.current_page == PAGE_CAL_EXEC) {
        menu_handle_cal_exec_buttons();
        return;
    }

    /* Xử lý phím riêng cho màn hình hiệu chuẩn DO (Zero/Slope) */
    if (g_menu.current_page == PAGE_CAL_DO_EXEC) {
        menu_handle_cal_do_exec_buttons();
        return;
    }

    /* Xử lý phím riêng cho màn hình hiệu chỉnh nhiệt độ DO */
    if (g_menu.current_page == PAGE_CAL_DO_TEMP) {
        menu_handle_cal_do_temp_buttons();
        return;
    }

    /* Xử lý phím riêng cho màn hình Hỏi xác nhận Reset cảm biến */
    if (g_menu.current_page == PAGE_CONFIRM_RESET) {
        menu_handle_confirm_reset_buttons();
        return;
    }

    /* Xử lý phím riêng cho màn hình Sức khỏe cảm biến pH */
    if (g_menu.current_page == PAGE_PH_HEALTH) {
        menu_handle_ph_health_buttons();
        return;
    }

    /* Xử lý phím riêng cho màn hình xem lịch sử dữ liệu FRAM */
    if (g_menu.current_page == PAGE_HISTORY_LOG) {
        menu_handle_history_log_buttons();
        return;
    }

    /* Xử lý phím riêng cho màn hình Quét WiFi */
    if (g_menu.current_page == PAGE_WIFI_SCAN_LIST) {
        menu_handle_wifi_scan_list_buttons();
        return;
    }

    /* Xử lý phím riêng cho màn hình Nhập Mật Khẩu WiFi (Character Picker) */
    if (g_menu.current_page == PAGE_WIFI_PASS_ENTRY) {
        menu_handle_wifi_pass_entry_buttons();
        return;
    }

    /* Xử lý phím riêng cho màn hình Trạng Thái WiFi */
    if (g_menu.current_page == PAGE_WIFI_STATUS) {
        menu_handle_wifi_status_buttons();
        return;
    }

    /* Xử lý phím riêng cho màn hình Thông Tin Thiết Bị */
    if (g_menu.current_page == PAGE_DEVICE_INFO) {
        menu_handle_device_info_buttons();
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
            s_menu_scroll_ticks = 0;
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
            s_menu_scroll_ticks = 0;
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
        } else if (child == PAGE_CHANGE_PIN) {
            menu_pin_begin_entry(PAGE_CHANGE_PIN);
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
    if (s_pin_mode == PIN_MODE_CHANGE_OLD) {
        if (g_sys_lang == LANG_VI) {
            LCD_DrawString(4, 2, "Nhap Mat Khau Cu", LCD_COLOR_OFF);
        } else {
            LCD_DrawString(4, 2, "Enter Old Password", LCD_COLOR_OFF);
        }
    } else if (s_pin_mode == PIN_MODE_CHANGE_NEW) {
        if (g_sys_lang == LANG_VI) {
            LCD_DrawString(4, 2, "Nhap Mat Khau Moi", LCD_COLOR_OFF);
        } else {
            LCD_DrawString(4, 2, "Enter New Password", LCD_COLOR_OFF);
        }
    } else if (s_pin_target_page == PAGE_SENSOR_SETTINGS) {
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
    draw_arrow_down_large(8, STATUS_BAR_Y + 3, LCD_COLOR_OFF);
    draw_arrow_up_large(32, STATUS_BAR_Y + 3, LCD_COLOR_OFF);
    draw_arrow_right_large(56, STATUS_BAR_Y + 3, LCD_COLOR_OFF);
    LCD_DrawString(80, STATUS_BAR_Y + 3, "ESC", LCD_COLOR_OFF);
    LCD_DrawString(104, STATUS_BAR_Y + 3, "ENT", LCD_COLOR_OFF);
    LCD_Flush();
}

static void menu_pin_handle_buttons(void)
{
    if (btn_edge(BTN_IDX_ESC)) {
        g_menu.in_pin_entry = false;
        s_pin_show_error = false;
        if (s_pin_mode != PIN_MODE_UNLOCK) {
            goto_page(PAGE_SYSTEM_SETTINGS);
        }
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
        if (s_pin_mode == PIN_MODE_CHANGE_OLD) {
            if (menu_pin_verify()) {
                s_pin_mode = PIN_MODE_CHANGE_NEW;
                memset(s_pin_entry, 0, sizeof(s_pin_entry));
                s_pin_cursor = 0;
                s_pin_show_error = false;
                s_pin_reveal = 0;
                g_lcd_need_redraw = true;
                ESP_LOGI(TAG_MENU, "Mat khau cu dung -> Chuyen sang nhap mat khau moi");
            } else {
                s_pin_show_error = true;
                g_lcd_need_redraw = true;
                ESP_LOGW(TAG_MENU, "Mat khau cu sai");
            }
        } else if (s_pin_mode == PIN_MODE_CHANGE_NEW) {
            char new_pin[MENU_PIN_LEN + 1];
            for (uint8_t i = 0; i < MENU_PIN_LEN; i++) {
                new_pin[i] = (char)('0' + s_pin_entry[i]);
            }
            new_pin[MENU_PIN_LEN] = '\0';

            memcpy(s_menu_pin_stored, new_pin, MENU_PIN_LEN + 1);
            Nvs_Write_String("menu_pin", s_menu_pin_stored);
            ESP_LOGI(TAG_MENU, "Da luu mat khau moi NVS: %s", s_menu_pin_stored);

            g_menu.in_pin_entry = false;
            s_pin_show_error = false;

            menu_show_alert_dialog(g_sys_lang == LANG_VI ? "Mat Khau" : "Password",
                                   g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESS!", true);
            goto_page(PAGE_SYSTEM_SETTINGS);
            g_lcd_need_redraw = true;
        } else {
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
}

void menu_render(void)
{
    s_menu_scroll_ticks++;

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

        const uint8_t date_x = 34, date_y = 16;
        const uint8_t time_x = 40, time_y = 28;
        const uint8_t guide_y = 40;
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
            const char *msg = (g_sys_lang == LANG_VI) ? "Da luu!" : "Saved!";
            uint8_t msg_w = (uint8_t)(strlen(msg) * 6);
            uint8_t msg_x = (uint8_t)((128 - msg_w) / 2);
            LCD_DrawString(msg_x, guide_y, msg, LCD_COLOR_ON);
        } else {
            const char *hint = (g_sys_lang == LANG_VI) ? "ENT:luu RIGHT:doi o"
                                                        : "ENT:save RIGHT:field";
            uint8_t hint_w = (uint8_t)(strlen(hint) * 6);
            uint8_t hint_x = (uint8_t)((128 - hint_w) / 2);
            LCD_DrawString(hint_x, guide_y, hint, LCD_COLOR_ON);
        }
    } else if (g_menu.current_page == PAGE_TEMP_SETTINGS) {
        menu_render_temp_settings();
    } else if (g_menu.current_page == PAGE_TEMP_LIN_COMP) {
        menu_render_temp_lin_comp();
    } else if (g_menu.current_page == PAGE_MODBUS_EDIT_ADDR) {
        menu_render_modbus_addr();
    } else if (g_menu.current_page == PAGE_CAL_EXEC) {
        menu_render_cal_exec();
    } else if (g_menu.current_page == PAGE_CAL_DO_EXEC) {
        menu_render_cal_do_exec();
    } else if (g_menu.current_page == PAGE_CAL_DO_TEMP) {
        menu_render_cal_do_temp();
    } else if (g_menu.current_page == PAGE_CONFIRM_RESET) {
        menu_render_confirm_reset();
    } else if (g_menu.current_page == PAGE_HISTORY_LOG) {
        menu_render_history_log();
    } else if (g_menu.current_page == PAGE_WIFI_SCAN_LIST) {
        menu_render_wifi_scan_list();
    } else if (g_menu.current_page == PAGE_WIFI_PASS_ENTRY) {
        menu_render_wifi_pass_entry();
    } else if (g_menu.current_page == PAGE_WIFI_STATUS) {
        menu_render_wifi_status();
    } else if (g_menu.current_page == PAGE_DEVICE_INFO) {
        menu_render_device_info();
    } else if (g_menu.current_page == PAGE_PH_HEALTH) {
        menu_render_ph_health();
    } else {

        /* ── 2. Danh sách mục (y=13..48) ── */
        uint8_t visible = page->item_count - g_menu.scroll_offset;
        if (visible > VISIBLE_ITEMS) visible = VISIBLE_ITEMS;

        for (uint8_t i = 0; i < visible; i++) {
            uint8_t idx = g_menu.scroll_offset + i;
            uint8_t y   = ITEMS_Y_START + i * ITEM_ROW_H;

            bool is_active_choice = false;
            const char *item_str = page->items[idx];
            char item_dyn_buf[48];

            if (g_menu.current_page == PAGE_CAL_2PT) {
                if (idx == 0) snprintf(item_dyn_buf, sizeof(item_dyn_buf), g_sys_lang == LANG_VI ? "1. Thap 4.00/%.1f" : "1. Low 4.00/%.1f", ph_cal.ph4_voltage_mv);
                else if (idx == 1) snprintf(item_dyn_buf, sizeof(item_dyn_buf), g_sys_lang == LANG_VI ? "2. Cao  7.00/%.1f" : "2. High 7.00/%.1f", ph_cal.ph7_voltage_mv);
                item_str = item_dyn_buf;
            } else if (g_menu.current_page == PAGE_CAL_3PT_G1) {
                if (idx == 0) snprintf(item_dyn_buf, sizeof(item_dyn_buf), g_sys_lang == LANG_VI ? "1. Thap 4.00/%.1f" : "1. Low 4.00/%.1f", ph_cal.ph4_voltage_mv);
                else if (idx == 1) snprintf(item_dyn_buf, sizeof(item_dyn_buf), g_sys_lang == LANG_VI ? "2. Trung 6.86/%.1f" : "2. Mid 6.86/%.1f", ph_cal.ph7_voltage_mv);
                else if (idx == 2) snprintf(item_dyn_buf, sizeof(item_dyn_buf), g_sys_lang == LANG_VI ? "3. Cao  9.18/%.1f" : "3. High 9.18/%.1f", ph_cal.ph10_voltage_mv);
                item_str = item_dyn_buf;
            } else if (g_menu.current_page == PAGE_CAL_3PT_G2) {
                if (idx == 0) snprintf(item_dyn_buf, sizeof(item_dyn_buf), g_sys_lang == LANG_VI ? "1. Thap 4.00/%.1f" : "1. Low 4.00/%.1f", ph_cal.ph4_voltage_mv);
                else if (idx == 1) snprintf(item_dyn_buf, sizeof(item_dyn_buf), g_sys_lang == LANG_VI ? "2. Trung 7.00/%.1f" : "2. Mid 7.00/%.1f", ph_cal.ph7_voltage_mv);
                else if (idx == 2) snprintf(item_dyn_buf, sizeof(item_dyn_buf), g_sys_lang == LANG_VI ? "3. Cao  10.0/%.1f" : "3. High 10.0/%.1f", ph_cal.ph10_voltage_mv);
                item_str = item_dyn_buf;
            }
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
                uint32_t cur_baud = (s_modbus_edit_port == 1) ? g_mb1_baud : g_mb2_baud;
                if (bauds[idx] == cur_baud) {
                    is_active_choice = true;
                }
            }
            else if (g_menu.current_page == PAGE_MODBUS_SELECT_PARITY) {
                uint8_t cur_parity = (s_modbus_edit_port == 1) ? g_mb1_parity : g_mb2_parity;
                if (idx == cur_parity) {
                    is_active_choice = true;
                }
            }
            else if (g_menu.current_page == PAGE_MODBUS_SELECT_STOP) {
                uint8_t stops[] = {1, 2};
                uint8_t cur_stop = (s_modbus_edit_port == 1) ? g_mb1_stop : g_mb2_stop;
                if (stops[idx] == cur_stop) {
                    is_active_choice = true;
                }
            }

            if (idx == g_menu.selected) {
                /* Mục được chọn: nền đen, chữ trắng */
                LCD_FillRect(0, y, 128, ITEM_ROW_H, LCD_COLOR_ON);

                // 1. Tính độ rộng thực tế của tên mục (mỗi ký tự rộng 6px)
                int16_t scroll_x = 0;
                uint16_t text_w = strlen(item_str) * 6;
                uint16_t view_w = 120 - 4; // Độ rộng khung hiển thị tối đa là 116px (khoảng 19 ký tự)

                // 2. Nếu tên mục dài hơn khung hiển thị -> Kích hoạt chạy chữ
                if (text_w > view_w) {
                    int16_t max_scroll = text_w - view_w; // Khoảng cách pixel tối đa cần dịch chuyển
                    
                    // Tổng thời gian 1 chu kỳ = 30 ticks dừng đầu (1.5s) + max_scroll ticks trượt + 30 ticks dừng cuối (1.5s)
                    uint32_t total_cycle = 30 + max_scroll + 30; 
                    uint32_t phase = s_menu_scroll_ticks % total_cycle; // Lấy pha hiện tại trong chu kỳ
                    
                    if (phase < 30) {
                        scroll_x = 0;                  // Phase 1: Dừng 1.5s ở đầu dòng cho người dùng đọc
                    } else if (phase < 30 + max_scroll) {
                        scroll_x = phase - 30;         // Phase 2: Trượt chữ từng 1px/50ms từ phải sang trái
                    } else {
                        scroll_x = max_scroll;         // Phase 3: Dừng 1.5s ở cuối dòng trước khi lặp lại
                    }
                    
                    // Yêu cầu task màn hình redraw liên tục để giữ tốc độ chạy chữ mượt mà
                    g_lcd_need_redraw = true;
                }

                // 3. Gọi hàm vẽ chữ cuộn với độ lệch scroll_x vừa tính
                LCD_DrawStringScroll(4, y + 1, item_str, 120, scroll_x, LCD_COLOR_OFF);

                /* Mũi tên ► ở bên phải để chỉ mục đang chọn */
                if (page->children[idx] != PAGE_LEAF) {
                    draw_arrow_right(121, y + 2, LCD_COLOR_OFF);
                }
                if (is_active_choice) {
                    LCD_DrawChar(112, y + 1, '*', LCD_COLOR_OFF);
                }
            } else {
                /* Mục bình thường: nền trắng, chữ đen, xén lề tối đa 120px */
                LCD_DrawStringScroll(4, y + 1, item_str, 120, 0, LCD_COLOR_ON);
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

    /* Mũi tên ▼ (DOWN) */
    draw_arrow_down_large(8, STATUS_BAR_Y + 3, LCD_COLOR_OFF);

    /* Mũi tên ▲ (UP) */
    draw_arrow_up_large(32, STATUS_BAR_Y + 3, LCD_COLOR_OFF);

    /* Mũi tên ► (RIGHT) */
    draw_arrow_right_large(56, STATUS_BAR_Y + 3, LCD_COLOR_OFF);

    /* "ESC" */
    LCD_DrawString(80, STATUS_BAR_Y + 3, "ESC", LCD_COLOR_OFF);

    /* "ENT" */
    LCD_DrawString(104, STATUS_BAR_Y + 3, "ENT", LCD_COLOR_OFF);

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
