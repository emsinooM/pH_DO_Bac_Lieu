Edited temp_settings.md
Searched for "PAGE_CAL_DO_EXEC"
Searched for "s_do_cal_exec"
Searched for "s_do_cal_exec"
Viewed screen_menu.c:1630-1720
Viewed screen_menu.c:1600-1630
Searched for "do_cal_exec_state_t"
Viewed screen_menu.c:60-75
Searched for "PAGE_CAL_DO"
Viewed screen_menu.c:2255-2285
Searched for "do_sensor_calibrate"
Viewed web_portal.c:1239-2038

Dưới đây là thông tin trích xuất toàn bộ luồng chức năng, giao diện hiển thị và các hàm thực thi liên quan đến hiệu chuẩn DO (**DO Calibration**: Zero, Slope và Reset) từ màn hình thiết bị và cả API trên Web Server:

---

### 1. Cây Menu hiệu chuẩn DO
Tại [screen_menu.c](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/screen_menu.c):
* Khi người dùng chọn mục **Hieu Chuan DO** từ menu Calibration, cây menu dạng tiếng Anh (dòng 234) hoặc tiếng Việt (dòng 482) sẽ hiển thị danh sách 4 mục con:
```c
// d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/screen_menu.c
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
```
* **Mục 1 & 2** cùng chuyển tới trang thực thi [PAGE_CAL_DO_EXEC](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/inc/screen_menu.h#L115). Hệ thống lưu phân biệt qua chỉ số `do_cal_type`: `0 = Zero`, `1 = Slope` khi chuyển trang (dòng 2143):
```c
} else if (child == PAGE_CAL_DO_EXEC) {
    s_do_cal_exec.do_cal_type = g_menu.selected;
}
```

---

### 2. Giao diện hiển thị thực thi hiệu chuẩn (Zero / Slope)
Tại [screen_menu.c](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/screen_menu.c#L1000-L1051):
```c
// d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/screen_menu.c
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
    snprintf(extra_str, sizeof(extra_str), "T:%.1f C  Sat:%.1f%%", status.do_temp_c, status.do_saturation_pct);
    uint8_t ext_w = strlen(extra_str) * 6;
    uint8_t ext_x = (LCD_WIDTH - ext_w) / 2;
    LCD_DrawString(ext_x, 36, extra_str, LCD_COLOR_ON);

    // 4. Thanh nút bấm phía dưới
    LCD_FillRect(0, STATUS_BAR_Y, 128, STATUS_BAR_H, LCD_COLOR_ON);
    LCD_DrawString(8, STATUS_BAR_Y + 3, g_sys_lang == LANG_VI ? "HUY" : "ESC", LCD_COLOR_OFF);
    LCD_DrawString(100, STATUS_BAR_Y + 3, g_sys_lang == LANG_VI ? "LUU" : "ENT", LCD_COLOR_OFF);
    
    LCD_Flush();
}
```

---

### 3. Logic xử lý nút bấm trên màn hình LCD
Tại [screen_menu.c](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/screen_menu.c#L1623-L1674):
* **Nút ESC:** Hủy hiệu chuẩn, quay về menu chọn [PAGE_CAL_DO](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/inc/screen_menu.h#L114).
* **Nút ENTER (Lưu):** Thực hiện lệnh ghi dữ liệu hiệu chuẩn xuống cảm biến:
```c
// d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/screen_menu.c
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
}
```

* **Xử lý mục Reset cảm biến (Mục thứ 4 - Node lá):**
Tại [screen_menu.c](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/screen_menu.c#L2262-L2282), khi bấm chọn dòng `"Reset Cam Bien"`:
```c
} else if (g_menu.current_page == PAGE_CAL_DO) {
    if (g_menu.selected == 3) { // Vị trí dòng thứ 4
        esp_err_t err = do_sensor_reset(); // Thực thi khôi phục cài đặt gốc
        if (err == ESP_OK) {
            menu_show_alert_dialog("Khoi Phuc DO", g_sys_lang == LANG_VI ? "Thanh Cong!" : "SUCCESSFUL!", true);
        } else {
            menu_show_alert_dialog("Khoi Phuc DO", g_sys_lang == LANG_VI ? "That Bai!" : "FAILED!", false);
        }
        goto_page(PAGE_CAL_DO);
        g_lcd_need_redraw = true;
    }
}
```

---

### 4. Chi tiết các hàm phần cứng gửi lệnh Modbus RTU
Tại [do_sensor.c](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/do_sensor.c) (Chịu trách nhiệm gói lệnh gửi đi RS485):

* **Hàm hiệu chuẩn điểm 0 (`CAL_DO_ZERO`):**
Ghi giá trị `0x0000` vào thanh ghi `0x1000`.
```c
esp_err_t do_sensor_calibrate_zero(void){
    if(!s_initialized) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return ESP_FAIL;

    // Ghi giá trị 0 vào thanh ghi 0x1000
    int err = modbus_write_reg(s_cfg.slave_id, 0x1000, 0x0000);
    xSemaphoreGive(s_mutex);

    if(err != 0) return ESP_FAIL;
    return ESP_OK;
}
```

* **Hàm hiệu chuẩn độ dốc (`CAL_DO_SLOPE`):**
Ghi giá trị `0x0000` vào thanh ghi `0x1004`.
```c
esp_err_t do_sensor_calibrate_slope(void){
    if(!s_initialized) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return ESP_FAIL;

    // Ghi giá trị 0 vào thanh ghi 0x1004
    int err = modbus_write_reg(s_cfg.slave_id, 0x1004, 0x0000);
    xSemaphoreGive(s_mutex);

    if(err != 0) return ESP_FAIL;
    return ESP_OK;
}
```

* **Hàm khôi phục cài đặt gốc (`RESET_DO_SENSOR`):**
Ghi giá trị `0x0000` vào thanh ghi `0x2020`.
```c
esp_err_t do_sensor_reset(void){
    if(!s_initialized) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return ESP_FAIL;

    // Ghi giá trị 0 vào thanh ghi 0x2020
    int err = modbus_write_reg(s_cfg.slave_id, 0x2020, 0x0000);
    xSemaphoreGive(s_mutex);

    if(err != 0) return ESP_FAIL;
    return ESP_OK;
}
```

---

### 5. API trên Web Server phục vụ điều khiển từ xa
Tại [web_portal.c](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/web_portal.c#L1647-L1770), API `/api/do_control` tiếp nhận gói tin POST dạng JSON để thực thi các lệnh tương đương:
```c
// d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/web_portal.c
static esp_err_t do_control_post_handler(httpd_req_t *req) {
  // ... [Đọc nội dung JSON nhận được] ...
  const cJSON *action = cJSON_GetObjectItem(root, "action");
  
  if (strcmp(action->valuestring, "CAL_DO_ZERO") == 0) {
    esp_err_t err = do_sensor_calibrate_zero(); // Gọi API giống hệt nút nhấn LCD
    if (err == ESP_OK) {
      status_msg = "Hiệu chuẩn điểm 0 thành công!";
      success = true;
    } else {
      status_msg = "Hiệu chuẩn điểm 0 thất bại! Vui lòng kiểm tra lại kết nối cảm biến.";
    }
  } else if (strcmp(action->valuestring, "CAL_DO_SLOPE") == 0) {
    esp_err_t err = do_sensor_calibrate_slope(); // Gọi API giống hệt nút nhấn LCD
    if (err == ESP_OK) {
      status_msg = "Hiệu chuẩn độ dốc thành công!";
      success = true;
    } else {
      status_msg = "Hiệu chuẩn độ dốc thất bại! Vui lòng kiểm tra lại kết nối cảm biến.";
    }
  } else if (strcmp(action->valuestring, "RESET_DO_SENSOR") == 0) {
    esp_err_t err = do_sensor_reset(); // Gọi API giống hệt nút nhấn LCD
    if (err == ESP_OK) {
      status_msg = "Khôi phục cài đặt gốc thành công!";
      success = true;
    } else {
      status_msg = "Khôi phục cài đặt gốc thất bại! Vui lòng kiểm tra lại kết nối cảm biến.";
    }
  }
  // ... [Phản hồi JSON lại Client] ...
}
```

---
**Tóm tắt công việc:**
* Tôi đã trích xuất toàn bộ luồng hoạt động của tính năng hiệu chuẩn DO (Zero, Slope, Reset).
* Làm rõ mối liên hệ giữa **Cây Menu**, **Hàm hiển thị LCD**, **Bộ lọc nút bấm điều khiển LCD**, các **Hàm Modbus RTU** ghi lệnh trực tiếp vào cảm biến, và cách **Web Server API** tích hợp đồng bộ.