# TỔNG HỢP TOÀN DIỆN KIẾN TRÚC HỆ THỐNG & NHẬT KÝ KỸ THUẬT DỰ ÁN MEBIECO
## (ESP32-S3 Industrial-Grade pH / DO / Temperature Monitoring & Automation System)

> **Tài liệu hành trang kỹ thuật (Technical Master Dossier)**  
> *Mục đích*: Lưu trữ, tổng hợp và hệ thống hóa toàn bộ kiến trúc phần cứng, phần mềm cấp thấp (low-level drivers), thuật toán xử lý tín hiệu, giao thức đám mây, cơ chế an toàn RTOS, và kinh nghiệm thực chiến đã được phát triển trong dự án quan trắc chất lượng nước công nghiệp MebiEco.

---

## 1. Kiến trúc Lõi Hệ thống & Quản trị RTOS (Core System Architecture & FreeRTOS)

### 1.1. Nền tảng Vi điều khiển & Phân phối Tài nguyên Phần cứng
* **Vi điều khiển trung tâm:** **ESP32-S3 (Xtensa® Dual-Core 32-bit LX7 @ 240 MHz)** với 8MB Flash ngoài và 512KB SRAM nội bộ. Tận dụng kiến trúc Dual-Core để phân chia ranh giới tác vụ độc lập:
  * **Core 0 (Networking & Cloud Core):** Đảm nhiệm các tiến trình giao tiếp mạng nặng, xử lý ngăn xếp TCP/IP (lwIP), Wi-Fi SoftAP/STA Manager, Web Config Portal (HTTP Server), MbedTLS và Azure IoT Hub Client.
  * **Core 1 (Real-Time Sensing & UI Core):** Đảm nhiệm các tác vụ thời gian thực nghiêm ngặt: Đọc bit-banging ADC 24-bit CS1237, chuỗi lọc tín hiệu số 5 tầng, truyền thông Modbus RS485 cảm biến DO quang học, quét bàn phím vật lý 5 nút, và render đồ họa màn hình LCD ST7565 qua SPI DMA.
* **Môi trường phát triển:** **ESP-IDF Framework (C/C++ Language)** phiên bản hiện đại, sử dụng cơ chế cấu hình tập trung qua `sdkconfig` và `Kconfig.projbuild`.

### 1.2. Thiết kế Đa nhiệm (FreeRTOS Multitasking) & Kiểm soát Bộ nhớ
* **Điều phối Tiến trình (Task Scheduling & Synchronization):**
  * `ph_temp_sensor_task` (Core 1, Priority 4): Đọc và xử lý tín hiệu analog pH/Temp mỗi 500ms.
  * `do_sensor_task` (Core 1, Priority 5): Polling Modbus RTU cảm biến DO quang học KOG206 qua RS485.
  * `lcd_demo_task` (Core 1, Priority 3): Điều phối chu kỳ quét phím 50ms, render UI màn hình đo/menu và đẩy dữ liệu qua SPI.
  * `system_startup_task` (Core 0, Priority 5): Khởi tạo tuần tự Wi-Fi $\rightarrow$ kiểm tra OTA Pending $\rightarrow$ SNTP Time Sync $\rightarrow$ Azure IoT Hub. Tự hủy an toàn sau khi hoàn tất boot.
  * `User_Http_Server_Task` (Core 0, Priority 5): Phục vụ Web Portal, Virtual LCD Emulation và REST APIs.
  * `User_Azure_Task` (Core 0, Priority 5): Điều phối kết nối Azure MQTT/TLS, xử lý Direct Methods và đẩy Telemetry.
  * `User_Fram_Task` (Core 1, Priority 3): Ghi dữ liệu cảm biến định kỳ 1 phút/lần vào chip Ferroelectric RAM (FRAM).
  * `Azure_Offline_Sync_Task` (Core 0, Priority 4): Tự động phát hiện và đồng bộ dữ liệu lịch sử offline từ FRAM lên Azure IoT Hub theo từng batch khi có mạng trở lại.
* **Bảo vệ Tài nguyên dùng chung:** Sử dụng `SemaphoreHandle_t` (Mutex) tại tất cả các điểm giao thoa dữ liệu (`s_sensor_status_mutex`, `azureMutex`, Mutex trong từng bộ lọc Moving Average/Spike/Kalman) nhằm ngăn chặn triệt để hiện tượng xung đột tài nguyên (Race Condition) giữa các Task chạy song song trên 2 Core.
* **Giám sát Stack/Heap:** Giám sát nghiêm ngặt `uxTaskGetStackHighWaterMark()` và `esp_get_free_heap_size()`, ngăn ngừa hiện tượng rò rỉ bộ nhớ (Memory Leak) và tràn Stack (Stack Overflow) khi hệ thống vận hành liên tục 24/7.

---

## 2. Bản đồ Phần cứng & Ma trận GPIO (Physical Pin Mapping & Hardware Topology)

| Ngoại vi / Giao tiếp | Chức năng | GPIO (ESP32-S3) | Đặc tính & Hướng tín hiệu |
| :--- | :--- | :---: | :--- |
| **CS1237 (pH ADC)** | **SCLK** | `GPIO 18` | Output (Software bit-bang clock) |
| | **DATA** | `GPIO 17` | Input (Software bit-bang data / DRDY detection) |
| **CS1237 (Temp ADC)** | **SCLK** | `GPIO 19` | Output (Software bit-bang clock) |
| | **DATA** | `GPIO 8` | Input (Software bit-bang data / DRDY detection) |
| **DS3231 RTC** | **SDA** | `GPIO 3` | Bidirectional I2C Data (kèm trở kéo lên 4.7kΩ) |
| | **SCL** | `GPIO 20` | Output I2C Clock (kèm trở kéo lên 4.7kΩ) |
| **Modbus Cổng 1 (Dự phòng/PLC)** | **TX / RX / DE** | `GPIO 40 / 38 / 39` | UART RS485 Half-Duplex (Chân DE chủ động điều khiển thu/phát) |
| **Modbus Cổng 2 (Cảm biến DO)** | **TX / RX / DE** | `GPIO 42 / 41 / 45` | UART1 RS485 Half-Duplex (Kết nối mặc định tới đầu dò DO KOG206) |
| **ST7565R LCD (SPI)** | **CS / DC / CLK / MOSI** | `GPIO 12 / 9 / 11 / 10` | SPI2_HOST (FSPI) 1MHz DMA transfer, Software CS control |
| **Bàn phím vật lý (5 nút)** | **ESC / DOWN / UP / RIGHT / ENTER** | `GPIO 13 / 14 / 21 / 47 / 48` | Input Active LOW (Internal Pull-up) |
| **FRAM SPI (FM25CL64 / MB85RS)** | **CS / CLK / MOSI / MISO** | `GPIO 46 / 15 / 16 / 7` | SPI Master 10MHz (Lưu log môi trường và lịch biểu thiết bị) |
| **LED Chỉ thị Trạng thái** | **LED Ext / LED Onboard** | `GPIO 41 / 42` | Output Active HIGH |

---

## 3. Giao tiếp Ngoại vi & Phát triển Driver Cấp thấp (Low-Level Driver Development)

### 3.1. Phân hệ Đo lường Analog qua Bộ chuyển đổi 24-bit ADC CS1237
* **Nguyên lý Giao tiếp Bit-Banging Tốc độ cao:** 
  * Tự thiết kế Driver bit-banging chuẩn xác theo giản đồ xung thời gian (Timing Diagram) của IC Sigma-Delta ADC CS1237.
  * Nhận diện tín hiệu sẵn sàng dữ liệu (DRDY) khi chân `DATA` được IC kéo xuống mức `LOW`, sau đó phát 24 xung `SCLK` để đọc dữ liệu nối tiếp (MSB first), tiếp tục phát thêm 3 hoặc 4 xung phụ để hoàn tất chu kỳ chuyển đổi hoặc cấu hình thanh ghi.
  * Cấu hình thanh ghi nội của cả 2 chip: **Tốc độ lấy mẫu 40Hz, Độ lợi PGA = 1, Kênh đầu vào vi phân A (0x10)**.
* **Cơ chế Tự phục hồi Phần cứng (ADC Hardware Watchdog):** 
  * Xây dựng bộ đếm giám sát lỗi truyền thông CS1237. Nếu mất tín hiệu hoặc kẹt xung liên tiếp $\ge 10$ lần (5 giây), hệ thống tự động tái cấu hình lại ma trận GPIO (`init_system_gpios()`) và ghi nạp lại thanh ghi cấu hình `CS1237_CFG_40HZ_PGA1_CHA` để cứu kênh đo mà không cần reset toàn bộ vi điều khiển.
* **Xử lý Dữ liệu Toán học Cấp bit:**
  * Giải mã định dạng **số bù 2 (Two's Complement) 24-bit signed integer** sang giá trị điện thế vi phân thực tế $V_{diff}$:
    $$V_{diff} = \frac{\text{Raw\_ADC} \times V_{ref}}{2^{23} - 1} = \frac{\text{Raw\_ADC} \times 3.302\text{V}}{8388607}$$
  * Khử offset và bù hệ số suy hao $0.5\times$ của mạch khuếch đại đệm Op-Amp vi sai hai lớp:
    $$V_{probe} = V_{diff} \times 2.0 \implies V_{probe\_mV} = V_{probe} \times 1000.0$$

### 3.2. Thuật toán Đo Nhiệt độ (PT1000 RTD & NTC Thermistor)
* **Cảm biến nhiệt độ công nghiệp PT1000:**
  * Mạch cầu phân áp độ chính xác cao với điện trở tham chiếu $R_{calib} = 750.0\,\Omega$ ($V_{bridge} = 3.302\text{V}$).
  * Công thức giải mã điện trở thực tế và quy đổi nhiệt độ:
    $$R_{PT1000} = R_{calib} \times \left(\frac{V_{bridge}}{V_{diff}} - 1\right) \implies T^\circ C = \frac{R_{PT1000} - 1000.0\,\Omega}{3.9083\,\Omega/^\circ C}$$
* **Cảm biến NTC 10K Beta 3950 (Tùy chọn):** Giải mã qua phương trình nhiệt phi tuyến Steinhart-Hart.
* **4 Chế độ đo nhiệt độ:** `ATC °C` (Tự động bù nhiệt độ C), `MTC °C` (Nhập nhiệt độ thủ công khi sensor hỏng), `ATC °F`, `MTC °F`. Hỗ trợ nhập hệ số bù lệch nhiệt độ thủ công (`g_temp_offset`).

### 3.3. Thuật toán Đo pH, Bù nhiệt Nernst & Kiểm tra Sức khỏe Điện cực (Sensor Health)
* **Tự động bù nhiệt Nernst (Automatic Temperature Compensation - ATC):**
  * Độ dốc lý thuyết biến thiên theo nhiệt độ tuyệt đối Kelvin ($T_K = T_C + 273.15$):
    $$S(T) = -59.16\text{ mV/pH} \times \frac{T_K}{298.15\text{K}}$$
  * Công thức tính pH thực tế sau hiệu chuẩn:
    $$\text{pH} = \text{pH}_{mid} + \frac{\frac{V_{probe\_mV}}{T_K} - U_7}{S_{norm}}$$
* **Cơ chế Bảo vệ An toàn Kép (Two-Tier Calibration Validation):**
  * **Cấp 1 (Màn hình LCD):** Kiểm tra dải điện áp tối ưu trước khi hiển thị nút `LƯU` (pH 4: $+140 \div +210$ mV, pH 7: $-30 \div +30$ mV, pH 6.86: $-20 \div +35$ mV, pH 9.18: $-160 \div -95$ mV, pH 10: $-210 \div -140$ mV).
  * **Cấp 2 (Thuật toán lõi):** Giới hạn điện áp an toàn tuyệt đối trước khi ghi NVS (pH 4: $+120 \div +240$ mV, pH 7: $-60 \div +60$ mV, pH 10: $-240 \div -100$ mV).
* **Đánh giá Sức khỏe Điện cực pH (Sensor Health Diagnostics):**
  * Tự động tính toán độ nhạy (Slope Efficiency) quy đổi về chuẩn $25^\circ C$.
  * Điện cực được xác nhận đạt chuẩn (`is_healthy = true`) khi thỏa mãn: Đã hiệu chuẩn thành công, độ nhạy Slope đạt **$80.0\% \div 110.0\%$** (tương đương $47.33 \div 65.08\text{ mV/pH}$), và Zero Offset nằm trong khoảng $[-30.0\text{ mV}, +30.0\text{ mV}]$.
* **Bù nhiệt độ Dung dịch Tuyến tính (Linear Solution Compensation):** Quy đổi pH tại nhiệt độ thực tế về chuẩn $25^\circ C$ với hệ số $\alpha$ tùy chỉnh từ $-10.00\%$ đến $+10.00\%/^\circ C$:
  $$\text{pH}_{25} = \frac{\text{pH}_{t}}{1 + \alpha \times (T^\circ C - 25.0)}$$

### 3.4. Phân hệ Cảm biến Oxy Hòa Tan Quang Học (Optical DO Sensor Modbus RS485)
* **Tích hợp Cảm biến DO Quang học KOG206:**
  * Giao tiếp Modbus RTU trên cổng phần cứng RS485 số 2 (`UART1`).
  * Điều khiển luồng bán song công (Half-Duplex) thủ công qua chân Driver Enable (`GPIO 45`). Chân `DE` được kéo lên `HIGH` trước khi gửi và hạ ngay về `LOW` sau khi byte cuối cùng rời thanh ghi shift của UART.
  * Tối ưu hóa thuật toán **Modbus CRC16 Bảng tra cứu đôi (Dual Lookup Table)** cho tốc độ giải mã tức thời.
* **Đọc Dữ liệu Đa tham số:** Thu thập đồng thời Nồng độ Oxy hòa tan ($mg/L$ hoặc $ppm$), Độ bão hòa Oxy ($HL\%$), và Nhiệt độ nội tại của đầu dò.
* **Tập lệnh Điều khiển & Hiệu chuẩn DO từ xa:**
  * Hiệu chuẩn điểm 0 (`do_sensor_calibrate_zero()` trong dung dịch Natri Sunfit $Na_2SO_3$ 5%).
  * Hiệu chuẩn độ dốc Slope 100% (`do_sensor_calibrate_slope()` trong không khí ẩm bão hòa).
  * Hiệu chỉnh bù nhiệt độ sensor (`do_sensor_correct_temp()`) và bù độ mặn nước biển/ao tôm (`do_sensor_set_salinity()`).
  * Khôi phục cài đặt xuất xưởng cảm biến (`do_sensor_reset()`).

---

## 4. Chuỗi Thuật toán Xử lý Tín hiệu & Lọc Nhiễu Số 5 Tầng (Multi-Stage Filtering Pipeline)

Trong môi trường ao nuôi tôm và công nghiệp nặng, xung nhiễu điện từ cực mạnh phát ra từ biến tần, máy quạt nước công suất lớn và bơm chìm thường gây sai lệch hoặc dao động ảo. MebiEco ứng dụng chuỗi lọc số 5 lớp phối hợp:

```
[ADC Raw / RS485 Raw]
        │
        ▼
┌─────────────────────────┐
│  1. Spike Filter        │ ──► Loại bỏ xung sét/nhiễu gai đột biến (ΔpH > 0.3, ΔT > 1.0°C)
└─────────────────────────┘     (Hỗ trợ Step Detection tự động Flush Buffer khi đổi dung dịch)
        │
        ▼
┌─────────────────────────┐
│  2. Median Filter       │ ──► Lọc trung vị cửa sổ 5 mẫu (Loại bỏ nhiễu hạt muối/xung lẻ)
└─────────────────────────┘
        │
        ▼
┌─────────────────────────┐
│  3. Moving Average O(1) │ ──► Lọc trung bình trượt 3 cấp độ: Low (5s), Medium (10s), High (20s)
└─────────────────────────┘     (Duy trì tổng cộng dồn running_sum với độ phức tạp O(1))
        │
        ▼
┌─────────────────────────┐
│  4. EWMA Filter (Temp)  │ ──► Lọc làm mịn hàm mũ cho nhiệt độ (alpha = 0.10)
└─────────────────────────┘
        │
        ▼
┌─────────────────────────┐
│  5. Adaptive 1D Kalman  │ ──► Ước lượng trạng thái tối ưu cho pH & DO
└─────────────────────────┘     (Triệt tiêu nhiễu nền mà không gây trễ đáp ứng động)
        │
        ▼
[Clean Value to Screen / Web / Cloud / Storage]
```

---

## 5. Giao diện Đồ họa LCD ST7565 & Hệ thống Menu Phân cấp 4 Tầng

### 5.1. Kiến trúc Đồ họa Framebuffer & Tối ưu SPI DMA
* **Màn hình LCD GMG12864-06D (128x64 pixels, Driver ST7565R):**
  * Quản lý bộ đệm RAM cục bộ 1024 bytes (`fb[8][128]`).
  * Tối ưu hóa chuỗi lệnh truyền SPI: Quét toàn bộ 8 page với độ bù cột 4-pixel (`LCD_COLUMN_OFFSET = 4`) qua DMA transfer tốc độ 1MHz, giúp màn hình đạt tần số quét mượt mà không nhấp nháy.
  * Bộ thư viện đồ họa tối ưu hóa: Điểm, đường thẳng Bresenham, hình chữ nhật đặc/rỗng, đường tròn, Bitmap 1-bit, phông chữ 5x7, phông số lớn Arial Bold 16x28 và phông số vừa 10x14.

### 5.2. Chế độ Hiển thị Đa dạng & Biểu đồ Sóng Thời gian thực (Real-time Trend Chart)
* **3 Chế độ đo số:**
  * **pH Screen:** Giá trị pH hiển thị font cực lớn 3x4 + Nhiệt độ + RTC.
  * **DO Screen:** Nồng độ DO ($mg/L$) font cực lớn 3x4 + Bão hòa % + Nhiệt độ + RTC.
  * **DUAL Screen:** Hiển thị song song pH và DO font 2x2 xếp chồng + Sat% + Temp.
* **Biểu đồ Sóng Thời gian thực (Real-Time Trend Chart):**
  * Nhấn nút `RIGHT` để chuyển đổi tức thời giữa hiển thị Số $\leftrightarrow$ Biểu đồ sóng.
  * Cửa sổ 24 điểm dữ liệu liên tục ($\sim 48$ giây), tự động vẽ trục tọa độ X/Y, vạch chia tỉ lệ và nhãn thời gian thực từ RTC.
  * Phân chia vùng trục Y phi tuyến tính: Tập trung 55% diện tích đồ thị vào dải pH nhạy cảm $6.0 \div 9.0\text{ pH}$ giúp kỹ thuật viên dễ dàng nhận diện biến động nhỏ.

### 5.3. Cây Menu Cài đặt Phân cấp & Cơ chế An toàn
* **Cấu trúc Menu 4 Cấp:**
  * **1. Cài Đặt Hệ Thống:** Ngôn ngữ (Anh/Việt), Định dạng ngày (`YYYY-MM-DD`, `DD-MM-YYYY`, `MM-DD-YYYY`), Chỉnh giờ thủ công (vẽ con trỏ gạch chân dưới từng trường Năm/Tháng/Ngày/Giờ/Phút/Giây, tự động kiểm tra năm nhuận), Độ tương phản (0-63), Tỷ số điện trở (0-7), Đổi mã PIN bảo mật.
  * **2. Cài Đặt Hiển Thị:** Chế độ pH, Chế độ DO, Chế độ Song song DUAL.
  * **3. Cài Đặt Modbus (Yêu cầu PIN):** Cấu hình Port 1 & Port 2 (Baudrate, Parity, Stop bits, Slave ID).
  * **4. Cài Đặt Cảm Biến (Yêu cầu PIN):** Hiệu chuẩn pH (2 điểm, 3 điểm, Reset), Xem sức khỏe điện cực pH, Cấu hình bộ lọc số (L/M/H), Cấu hình bù nhiệt độ & alpha, Hiệu chuẩn DO (Zero/Slope/Temp/Salinity).
  * **5. Nhật Ký Dữ Liệu (History Log):** Xem trực tiếp dữ liệu lịch sử lưu trong FRAM ngay trên màn hình LCD.
  * **6. Quản Lý Wi-Fi:** Quét danh sách mạng xung quanh và nhập mật khẩu Wi-Fi bằng bàn phím 5 nút trên LCD.
* **Phím tắt Khởi động lại An toàn (Safe Reboot Shortcut):** Nhấn giữ phím `ENTER` liên tục trong 7 giây tại bất kỳ màn hình nào để kích hoạt khởi động lại phần mềm an toàn.

---

## 6. Cổng Web Cấu hình Cục bộ (Advanced Local Web Portal & Remote Emulation)

* **Hạ tầng Web Server Không Dây:**
  * ESP32-S3 phát Wi-Fi Access Point độc lập (`MEBICO_ESP32_PH_xxxx` kèm MAC) kết hợp mDNS (`mebi-ph.local`).
  * Giao diện Responsive Web UI hiện đại (Dark Mode / Light Mode, hiệu ứng Glassmorphism) chạy hoàn toàn trong bộ nhớ flash của ESP32.
* **Màn hình LCD Ảo & Bàn phím Giả lập từ xa (Virtual LCD Canvas & Remote Keypad):**
  * API `/api/screen_fb`: Đọc trực tiếp 1024 bytes Framebuffer từ RAM vi điều khiển và render pixel-by-pixel lên thẻ HTML5 Canvas trên trình duyệt điện thoại/máy tính.
  * API `/api/simulate_btn`: Nhận lệnh bấm phím ảo từ xa (`ESC`, `UP`, `DOWN`, `RIGHT`, `ENTER`), cho phép kỹ thuật viên điều khiển toàn bộ menu thiết bị từ xa qua Wi-Fi y hệt như đang bấm phím vật lý.
* **Hệ thống Terminal Log Web 16KB (Live Web Terminal):**
  * Hook trực tiếp hàm `esp_log_set_vprintf` vào một Ring Buffer 16KB trong RAM.
  * Cung cấp API `/api/terminal_log` giúp lập trình viên/kỹ thuật viên theo dõi toàn bộ log hệ thống thời gian thực ngay trên trình duyệt mà không cần kết nối dây Serial/UART.
* **Xác thực Bảo mật & Tính năng Nâng cao:**
  * Cơ chế phiên đăng nhập quản trị viên (Admin Session Cookie).
  * Đồng bộ giờ tức thời từ trình duyệt vào chip RTC DS3231 (`/api/set_time`).
  * Công cụ bắn thử nghiệm Telemetry lên Azure Cloud (`/api/push_fake_telemetry`).
  * Danh sách quản lý đa mạng Wi-Fi (Multi-WiFi Profile Manager).

---

## 7. Tích hợp Đám Mây Azure IoT Hub & Cơ chế Lưu trữ Offline FRAM

### 7.1. Giao tiếp Đám mây Doanh nghiệp (Enterprise Azure IoT Hub Client)
* **Bảo mật & Kết nối:**
  * Giao thức MQTT qua TLS với cơ chế sinh chuỗi xác thực SAS Token tự động bằng thuật toán mã hóa **HMAC-SHA256** từ Symmetric Key thiết bị + Epoch Expiry Time.
  * Tự động tái kết nối thông minh với thuật toán **Exponential Backoff Retry**.
  * **Watchdog Mạng 5 phút:** Tự động phát hiện mất mạng kéo dài để tái khởi tạo ngăn xếp mạng an toàn.
* **Bộ 8 Lệnh Điều Khiển Từ Xa (Azure Direct Methods):**
  * `501 (CMD_CODE_UPDATE_FIRMWARE)`: Nâng cấp Firmware OTA qua Cloud.
  * `502 (CMD_CODE_ASK_VERSION)`: Đọc phiên bản phần mềm và phân vùng đang chạy.
  * `503 (CMD_CODE_GET_PH)`: Bật/tắt/điều chỉnh chu kỳ đẩy dữ liệu On-Demand.
  * `504 (CMD_CODE_PH_CALIBRATE)`: Kích hoạt hiệu chuẩn pH từ xa (2 điểm, 3 điểm, Reset).
  * `505 (CMD_CODE_SET_DEVICE_CONFIG)`: Thay đổi cấu hình Wi-Fi, bộ lọc, Azure từ xa.
  * `506 (CMD_CODE_DO_CALIBRATE)`: Kích hoạt hiệu chuẩn DO từ xa (Zero, Slope, Salinity, Reset).
  * `507 (CMD_CODE_REBOOT)`: Khởi động lại thiết bị từ xa.
  * `508 (CMD_CODE_GET_FULL_TELEMETRY)`: Truy vấn toàn bộ chẩn đoán hệ thống (pH, DO, Temp, RSSI, Free Heap, Uptime, Reset Reason, OTA Partition, Điện áp $V_{probe}$).

### 7.2. Bộ nhớ Bền vững FRAM & Cơ chế Đồng bộ Dữ liệu Offline (Code 510)
* **Giao tiếp Chip FRAM qua SPI Master 10MHz:**
  * Bộ nhớ Ferroelectric RAM công nghiệp có độ bền ghi xóa $\approx 10^{12}$ lần, không có độ trễ ghi (zero write delay), không làm hao mòn flash.
* **Cấu trúc Ring Buffer Log Môi trường:**
  * Lưu trữ liên tục mỗi 1 phút: Timestamp epoch, pH (x100), Temp (x100), DO (x100), các cờ trạng thái `is_calibrated` và `do_valid`.
  * Header quản lý con trỏ: `head_index`, `tail_index`, `record_count`, `synced_index`.
* **Cơ chế Đồng bộ Offline (Offline Telemetry Sync - Code 510):**
  * Khi mất kết nối Internet/Wi-Fi, hệ thống tiếp tục ghi log môi trường vào FRAM mà không bị gián đoạn.
  * Khi có mạng trở lại, task `Azure_Offline_Sync_Task` tự động đọc các bản ghi chưa đồng bộ theo từng batch (`Fram_Log_Get_Unsynced_Batch`), đóng gói thành các payload JSON mang mã `Code: 510` gửi lên Azure IoT Hub, sau đó commit vị trí `synced_index`. Đảm bảo **không bao giờ mất dữ liệu quan trắc ao nuôi**.

---

## 8. Cơ chế Cập nhật OTA Chống Brick & Bài Học Thực Chiến FreeRTOS

### 8.1. Phân tích Cạm bẫy `vTaskDelete()` Khi Task Giữ Mutex hoặc Socket
> **Bài học xương máu (Lessons Learned):** Không bao giờ gọi `vTaskDelete(handle)` cưỡng bức một Task từ bên ngoài nếu Task đó có khả năng đang nắm giữ Mutex, Semaphore hoặc đang trong quá trình giao tiếp Socket TCP/TLS.
* **Hậu quả 1 (Deadlock vĩnh viễn):** Task bị hủy khi đang giữ Mutex $\rightarrow$ Mutex bị khóa vĩnh viễn vì FreeRTOS kernel không tự động trả Mutex $\rightarrow$ Mọi Task khác yêu cầu Mutex này sẽ bị treo cứng (Hang).
* **Hậu quả 2 (Rò rỉ Network Socket):** Task bị hủy khi đang gọi các hàm socket nghẽn (`connect()`, `recv()`) $\rightarrow$ Socket rơi vào trạng thái Half-Open, driver lwIP/MbedTLS bị kẹt ở lần kết nối tiếp theo.
* **Hậu quả 3 (Phân mảnh RAM):** Stack và TCB của Task bị xóa chỉ được giải phóng bởi `Idle Task` (Priority 0). Nếu CPU bị chiếm dụng bởi Task ưu tiên cao, RAM sẽ không bao giờ được thu hồi.

### 8.2. Giải pháp "Reboot-to-Clean-State" (Reboot-to-OTA) Hoàn Hảo
* **Quy trình Thực thi:**
  1. Khi nhận lệnh OTA (từ Cloud Code 501 hoặc Web Portal): Ghi URL firmware và cờ `ota_pending = 1` vào bộ nhớ NVS.
  2. Phản hồi mã ACK thành công về cho Server/Web Client, khởi tạo timer đếm lùi 1.5 giây rồi gọi `esp_restart()`.
  3. **Lần khởi động lại thứ nhất (RAM sạch tuyệt đối ~186KB Free Heap):** Vừa kết nối Wi-Fi thành công, chưa bật Azure IoT Hub, chưa bật Time Task, chưa bật các tiến trình nặng:
     - Đọc NVS thấy `ota_pending == 1` $\rightarrow$ Xóa cờ ngay lập tức (chống bootloop) và bắt đầu tải firmware.
     - Sử dụng bộ đệm HTTP mở rộng 8KB (`config.buffer_size = 8192`) giúp tăng tốc độ nạp firmware gấp 4--8 lần.
  4. Nạp thành công/thất bại $\rightarrow$ Ghi kết quả (`ota_res`, `ota_err`) vào NVS và tự động `esp_restart()` (Lần 2) vào firmware mới.
* **Bảng phân vùng Flash Kép (Dual-Bank 16MB `partitions.csv`):**
  * Gồm `factory` (3.5MB), `ota_0` (3.5MB), `ota_1` (3.5MB), `storage` (5MB SPIFFS), `nvs` (64KB), `otadata` (8KB).
  * Cơ chế tự động **Rollback** của Bootloader: Nếu firmware mới bị lỗi Crash/Watchdog Trigger khi vừa boot, Bootloader tự động chuyển quyền ưu tiên boot về phân vùng ổn định trước đó. **Triệt tiêu 100% rủi ro biến thiết bị thành "cục gạch" (Anti-Brick)**.

---

## 9. Quản lý Thời gian Thực Kép (DS3231 Hardware RTC + SNTP Sync)

* **Phần cứng RTC DS3231:** Giao tiếp I2C Master địa chỉ `0x68` (SDA `GPIO 3`, SCL `GPIO 20`).
* **Kiểm tra Cờ Mất Nguồn (Oscillator Stop Flag - OSF):** Khởi động kiểm tra thanh ghi trạng thái, phát hiện pin nuôi RTC bị yếu hoặc mất nguồn để cảnh báo người dùng.
* **Cố định Múi giờ UTC+7:** Thiết lập `setenv("TZ", "UTC-7", 1)` ngay từ đầu hàm `app_main()`, loại bỏ rủi ro sai lệch múi giờ POSIX.
* **Tự động Đồng bộ SNTP qua Wi-Fi:** Khi có Internet, SNTP Client đồng bộ thời gian từ các máy chủ Google NTP (`time.google.com`, `time1.google.com`, `time2.google.com`). Hàm callback `time_sync_cb` tự động ghi ngược thời gian Internet chuẩn xác vào IC DS3231 để duy trì thời gian ngoại tuyến.

---

## 10. Tích hợp Hệ thống Cơ điện & Tự động hóa Ao Nuôi Thực Địa

* **Đọc Bản vẽ Kỹ thuật & Đấu nối Tủ điện Công nghiệp:**
  * Thành thạo sơ đồ mạch động lực 3 pha và mạch điều khiển trong tủ điện nuôi tôm công nghệ cao.
  * Lắp ráp và đấu nối an toàn: **MCCB 3 pha/1 pha, Khởi động từ (Contactor), Rơ-le nhiệt (Thermal Overload Relay), Rơ-le kiếng trung gian 14 chân (Glass Relay)**.
* **Phân hệ Điều khiển Thiết bị Phụ trợ (`user_ouput.h`, `user_output.c`):**
  * Điều khiển 4 nhóm thiết bị ao nuôi chủ lực: `PaddleWheel_1` (Dàn quạt nước 1), `PaddleWheel_2` (Dàn quạt nước 2), `AirBlow` (Máy sục khí Oxy béc), `Syphon` (Bơm xả đáy/Xi-phông).
  * Bộ điều phối lịch biểu tự động: Quản lý tối đa 15 khung giờ biểu (`Device_Schedule_t`) độc lập cho từng thiết bị (`startTime`, `stopTime`, `runTime`, `pauseTime`). Cấu hình lịch biểu được lưu bền vững vào FRAM.
* **Tích hợp Điều khiển Biến tần Công nghiệp (Inverter Integration):**
  * Nghiên cứu sơ đồ chân điều khiển của các dòng biến tần công nghiệp phổ biến trong ao nuôi tôm (**Arinco AV18, Kaman KM600, Mitsubishi FR-A700/D700**).
  * Kết nối an toàn các chân GPIO từ ESP32-S3 qua mạch đệm rơ-le kiếng tới các chân lệnh kích hoạt chạy/dừng (**Forward/Stop - FWD/COM**) trên biến tần, cho phép bật/tắt motor quạt nước êm ái, chống sụt áp lưới điện ao nuôi và điều khiển từ xa an toàn qua ứng dụng di động.

---

## 11. Bảng Tóm Tắt Mã Lệnh & Kiến Trúc Dữ Liệu Toàn Cục

### 11.1. Bảng Mã Lệnh Đám Mây & Hệ Thống (Command Codes)

| Mã lệnh (Code) | Tên định danh | Ý nghĩa chức năng |
| :---: | :--- | :--- |
| **501** | `CMD_CODE_UPDATE_FIRMWARE` | Kích hoạt nâng cấp phần mềm từ xa (Fail-Safe OTA) |
| **502** | `CMD_CODE_ASK_VERSION` | Đọc phiên bản phần mềm, phân vùng nạp |
| **503** | `CMD_CODE_GET_PH` | Bật/tắt stream Telemetry tức thời theo chu kỳ |
| **504** | `CMD_CODE_PH_CALIBRATE` | Hiệu chuẩn cảm biến pH từ xa (2 điểm / 3 điểm / Reset) |
| **505** | `CMD_CODE_SET_DEVICE_CONFIG` | Cấu hình bộ lọc, Wi-Fi, tham số đám mây |
| **506** | `CMD_CODE_DO_CALIBRATE` | Hiệu chuẩn & cấu hình cảm biến Oxy hòa tan DO từ xa |
| **507** | `CMD_CODE_REBOOT` | Khởi động lại vi điều khiển an toàn |
| **508** | `CMD_CODE_GET_FULL_TELEMETRY` | Truy vấn toàn bộ Telemetry & Chẩn đoán hệ thống |
| **510** | `CMD_CODE_OFFLINE_TELEMETRY_SYNC` | Tự động đồng bộ các gói dữ liệu lịch sử lưu trong FRAM lên Cloud |

### 11.2. Cấu trúc Payload Telemetry JSON Chuẩn

```json
{
  "payload": {
    "HostName": "dev-iot-hub-1.azure-devices.net",
    "DeviceId": "MebiEco_pH_DO_BacLieu_01",
    "Code": 508,
    "TimeStamp": 1779878400,
    "SensorData": {
      "ph": 7.52,
      "ph_temp": 28.50,
      "Valid": true,
      "do": 6.85,
      "temp": 28.40,
      "do_sat": 95.20,
      "do_valid": true,
      "rssi": -62,
      "uptime": 86400,
      "reset_reason": "Power-on Reset",
      "free_ram": 245120,
      "v_probe_mv": 8.42,
      "do_err": 0,
      "ver": "2.0.2",
      "ota_part": "ota_0"
    }
  }
}
```

---

## 12. Tổng kết Giá trị & Hành trang Phát triển Tương lai

Hệ thống **MebiEco (ESP32-S3 pH/DO/Temp & Industrial Controller)** là sự kết hợp hoàn chỉnh giữa:
1. **Phần cứng công nghiệp ổn định:** Mạch custom 2 lớp, cách ly tín hiệu, bảo vệ quá áp/đảo cực, ADC vi sai 24-bit, RS485 kép và màn hình đồ họa ST7565.
2. **Kỹ thuật phần mềm vững chắc:** Xử lý tín hiệu 5 lớp (Spike + Median + Moving Avg + EWMA + Kalman), RTOS đa nhiệm không deadlock, kiến trúc Reboot-to-Clean-State cho OTA, lưu trữ vòng tròn trên FRAM chống mất dữ liệu khi mất điện.
3. **Trải nghiệm người dùng thông minh:** Giao diện đồ họa LCD song ngữ có biểu đồ sóng, bàn phím cơ 5 nút có phím tắt an toàn, Cổng Web Portal cục bộ giả lập toàn bộ màn hình/phím bấm và bộ log 16KB.
4. **Hệ sinh thái Đám mây hiện đại:** Đồng bộ Azure IoT Hub bảo mật SAS Token, 8 Direct Methods điều khiển/hiệu chuẩn toàn diện, và đồng bộ dữ liệu ngoại tuyến thông minh.

Toàn bộ các giải pháp kiến trúc, thuật toán, mã nguồn và bài học kinh nghiệm trên tạo nên nền tảng vững chắc để tiếp tục mở rộng thêm các loại cảm biến mới (Độ mặn EC, Độ đục Turbidity, ORP, $NH_4^+$, $NO_2^-$) và nâng cấp thuật toán điều khiển tự động hóa thông minh (AI / Fuzzy Logic / PID Controller) trong các thế hệ sản phẩm tiếp theo.