Searched for "time"
Viewed screen_menu.c:1-800
Viewed screen_menu.c:801-1600
Viewed screen_menu.c:1601-2400
Viewed user_time.c:1-245
Viewed ds3231.h:1-51
Viewed ds3231.c:1-165
Searched for "date"
Searched for "g_date_format"
Viewed screen_disp.c:1-800

Dưới đây là tổng hợp toàn bộ các chức năng code, cấu trúc dữ liệu và luồng xử lý liên quan đến **Ngày tháng / Cài đặt giờ** trong dự án:

---

### 1. Cấu trúc dữ liệu & Biến toàn cục

- **[date_format_t](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/inc/screen_menu.h#L24-L28)**: Kiểu liệt kê các định dạng ngày hiển thị:
  - `DATE_FORMAT_YYYY_MM_DD` (0: `YYYY-MM-DD`)
  - `DATE_FORMAT_DD_MM_YYYY` (1: `DD-MM-YYYY`)
  - `DATE_FORMAT_MM_DD_YYYY` (2: `MM-DD-YYYY`)
- **[g_date_format](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/screen_menu.c#L35)**: Biến lưu trữ định dạng ngày hiện tại của hệ thống.
- **[time_edit_state_t](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/screen_menu.c#L41-L51)**: Struct quản lý trạng thái khi người dùng chỉnh sửa ngày giờ:
  - Các biến: `year`, `month`, `day`, `hour`, `minute`, `second`.
  - `active_field`: Trường đang được chọn để chỉnh sửa (0: Year, 1: Month, 2: Day, 3: Hour, 4: Minute, 5: Second).
- **[get_days_in_month()](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/screen_menu.c#L90-L104)**: Hàm tính số ngày tối đa trong tháng (có xử lý năm nhuận).

---

### 2. Cấu trúc Cây Menu Ngày tháng / Cài đặt giờ

Trong file [screen_menu.c](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/screen_menu.c):
1. **Menu Cài đặt Hệ thống (`PAGE_SYSTEM_SETTINGS`)**: Gồm mục `1.2 Date` / `1.2 Ngày Tháng`.
2. **Menu Ngày tháng (`PAGE_DATE`)**: Gồm 2 mục con:
   - `1.2.1 Day Format` (`PAGE_DATE_FORMAT`)
   - `1.2.2 Time Settings` (`PAGE_TIME_SETTINGS`)
3. **Menu Định dạng ngày (`PAGE_DATE_FORMAT`)**: Chọn giữa `YYYY-MM-DD`, `DD-MM-YYYY`, `MM-DD-YYYY`.

---

### 3. Xử lý logic & Giao diện Chỉnh sửa Giờ

#### A. Khởi tạo trạng thái chỉnh sửa
Khi chuyển sang trang `PAGE_TIME_SETTINGS` trong [goto_page()](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/screen_menu.c#L647-L662):
- Đọc giờ hệ thống hiện tại bằng `time()` và `localtime_r()`.
- Nạp các giá trị thời gian hiện tại vào struct `s_time_edit`.
- Đặt `active_field = 0` (bắt đầu trỏ vào **Năm**).

#### B. Hiển thị màn hình Cài đặt Giờ
Hàm [menu_render_time_settings()](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/screen_menu.c#L779-L851):
- Hiển thị Title bar: `"Cai Dat Thoi Gian"` / `"Time Settings"`.
- Hiển thị 2 dòng: Ngày (`YYYY - MM - DD`) và Giờ (`HH : MM : SS`).
- Tự động vẽ đường gạch chân dày 2 pixel bên dưới trường (`active_field`) đang được chỉnh sửa.
- Hiển thị hướng dẫn phím ở Status bar: `ESC` (Hủy), `-/+` (Tăng/Giảm), `NEXT/TIEP` (Chuyển trường/Lưu).

#### C. Bắt sự kiện phím bấm
Hàm [menu_handle_time_settings_buttons()](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/screen_menu.c#L853-L973):
- **Phím ESC**: Hủy chỉnh sửa, thoát về menu `PAGE_DATE`.
- **Phím UP / DOWN**: Tăng hoặc giảm giá trị của trường đang chọn (`active_field`):
  - Năm: `2000` - `2099`.
  - Tháng: `1` - `12` (xoay vòng).
  - Ngày: `1` - `max_day` (tự động kiểm tra số ngày tối đa theo tháng & năm nhuận bằng `get_days_in_month`).
  - Giờ: `0` - `23`.
  - Phút / Giây: `0` - `59`.
- **Phím ENTER**:
  - Nếu `active_field < 5`: Tăng `active_field` để chuyển sang trường tiếp theo (Năm -> Tháng -> Ngày -> Giờ -> Phút -> Giây).
  - Khi ở trường **Giây** (`active_field == 5`): 
    1. Chuyển đổi thành cấu trúc `struct tm`.
    2. Ghi thời gian mới vào IC Real-Time Clock **DS3231** qua [ds3231_set_time()](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/ds3231.c#L99-L121).
    3. Ghi thời gian mới vào đồng hồ hệ thống ESP32 qua `settimeofday()`.
    4. Quay về menu `PAGE_DATE`.

---

### 4. Thay đổi & Lưu trữ Định dạng Ngày

- Khi người dùng chọn định dạng mới tại menu `PAGE_DATE_FORMAT` trong [menu_handle_buttons()](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/screen_menu.c#L2164-L2173):
  - Cập nhật biến `g_date_format`.
  - Lưu vào bộ nhớ Flash NVS bằng `Nvs_Write_Number("date_fmt", g_date_format)`.
- Khi khởi động thiết bị trong [menu_init()](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/screen_menu.c#L1571-L1579):
  - Tự động đọc lại giá trị `date_fmt` từ Flash NVS.

---

### 5. Giao tiếp phần cứng RTC DS3231

Nằm trong [ds3231.c](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/ds3231.c) & [ds3231.h](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/inc/ds3231.h):
- [ds3231_init()](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/ds3231.c#L19-L55): Khởi tạo bus I2C master kết nối với DS3231 (địa chỉ `0x68`).
- [ds3231_get_time()](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/ds3231.c#L57-L97): Đọc 7 byte dữ liệu BCD từ DS3231 và quy đổi ra `struct tm`.
- [ds3231_set_time()](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/ds3231.c#L99-L121): Mã hóa `struct tm` sang dạng BCD, nạp vào thanh ghi thời gian DS3231 và xóa cờ mất nguồn OSF bằng `ds3231_clear_oscillator_flag()`.

---

### 6. Đồng bộ thời gian qua SNTP (Wi-Fi)

Trong file [user_time.c](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/user_time.c):
- [User_Get_time()](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/user_time.c#L149-L188): Khi kết nối Wi-Fi, kích hoạt SNTP đồng bộ thời gian từ máy chủ NTP (`time.google.com`, `time1.google.com`, ...).
- [time_sync_cb()](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/user_time.c#L70-L94): Khi SNTP đồng bộ thành công, hệ thống tự động ghi giờ Internet vừa nhận được vào phần cứng RTC DS3231 để duy trì thời gian thực khi mất điện hoặc mất Wi-Fi.

---

### 7. Hiển thị Ngày tháng / Thời gian trên Màn hình Đo Lường LCD

Trong file [screen_disp.c](file:///d:/Study/STUdied/Mebi/CS1237_ESP32/main/src/screen_disp.c#L680-L706):
- Đọc thời gian hệ thống định kỳ.
- Format định dạng ngày (`date_str`) dựa theo `g_date_format` (`%d-%m-%Y`, `%m-%d-%Y`, hoặc `%Y-%m-%d`).
- Format định dạng giờ (`time_str`) dạng AM/PM (`%I:%M %p`).
- Vẽ ngày tháng và giờ lên thanh trạng thái dưới cùng (Bottom bar) của màn hình LCD ST7565 (dòng 795-796).