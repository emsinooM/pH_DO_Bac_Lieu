# TÀI LIỆU GIỚI THIỆU SẢN PHẨM & HỒ SƠ KỸ THUẬT
## HỆ THỐNG GIÁM SÁT CHẤT LƯỢNG NƯỚC CÔNG NGHIỆP MEBIECO
### (ESP32-S3 High-Precision pH / DO / Temperature Monitoring System)

---

## 1. TỔNG QUAN SẢN PHẨM

**MebiEco** là hệ thống giám sát chất lượng nước đa thông số cấp công nghiệp thế hệ mới, được thiết kế và tối ưu hóa đặc biệt cho **nuôi trồng thủy sản công nghệ cao** (như mô hình nuôi tôm/cá mật độ cao tại Bạc Liêu và ĐBSCL), **hệ thống xử lý nước thải**, **nước cấp công nghiệp** và **quan trắc môi trường tự động**.

Sản phẩm ứng dụng vi xử lý **ESP32-S3 (Dual-Core 240MHz)** tích hợp bộ chuyển đổi tương tự-số (ADC) 24-bit chuyên dụng, cảm biến quang học truyền thông RS485 Modbus RTU, kết hợp cùng hệ thống lọc nhiễu số đa tầng thích ứng (Adaptive Kalman Filter). MebiEco không chỉ mang lại khả năng đo đạc đạt độ chính xác cao, mà còn hỗ trợ theo dõi tại chỗ qua màn hình LCD đồ họa và điều khiển/giám sát từ xa qua **Web Config Portal** cùng nền tảng điện toán đám mây **Azure IoT Hub**.

---

## 2. CÁC TÍNH NĂNG NỔI BẬT & ƯU THẾ CẠNH TRANH

### 2.1. Đo lường đa thông số "Vàng" trong môi trường nước
- **pH (Độ kiềm/axit)**: Đo lường liên tục với điện cực thủy tinh độ nhạy cao qua ADC 24-bit CS1237.
- **DO (Oxy hòa tan)**: Đo nồng độ oxy ($mg/L$) và độ bão hòa ($HL\%$) bằng cảm biến quang học Modbus RS485 (KOG206), cho độ bền vượt trội, không cần thay thế dung dịch điện phân định kỳ như cảm biến cực phổ truyền thống.
- **Nhiệt độ (Temperature)**: Đo nhiệt độ dung dịch chính xác qua đầu dò PT1000/NTC, phục vụ tự động bù nhiệt.

### 2.2. Xử lý tín hiệu & Thuật toán lọc nhiễu số 5 tầng (Multi-Stage Digital Filtering)
Để loại bỏ nhiễu điện từ trong môi trường ao nuôi (do máy quạt nước, biến tần, máy bơm), MebiEco tích hợp chuỗi lọc số tân tiến:
1. **Spike Filter**: Tự động nhận diện và loại bỏ các xung nhiễu điện áp đột biến ($\Delta pH > 0.3$, $\Delta T > 1.0^\circ C$).
2. **Median Filter (Lọc trung vị)**: Cửa sổ 5 mẫu loại bỏ nhiễu hạt muối/nhiễu xung.
3. **Moving Average (Lọc trung bình trượt)**: Tùy chỉnh 3 cấp độ: Thấp (5s), Vừa (10s), Cao (20s) phù hợp với từng độ động của môi trường.
4. **EWMA (Exponentially Weighted Moving Average)**: Làm mịn biến thiên nhiệt độ.
5. **Adaptive 1D Kalman Filter (Bộ lọc Kalman 1 chiều tự thích ứng)**: Ước lượng trạng thái tối ưu cho pH và DO, triệt tiêu nhiễu nền mà không gây trễ tín hiệu.

### 2.3. Hiệu chuẩn linh hoạt & Bù nhiệt thông minh
- **Hiệu chuẩn pH**: Hỗ trợ **hiệu chuẩn 2 điểm** (pH 4.00, 7.00) hoặc **3 điểm** chuẩn quốc tế NIST (4.00, 6.86, 9.18) và chuẩn Âu/Mỹ (4.00, 7.00, 10.00). Kiểm tra ngưỡng điện áp an toàn trước khi lưu.
- **Hiệu chuẩn DO**: Hiệu chuẩn điểm 0 (Zero Calib trong dung dịch $Na_2SO_3$) và độ dốc (Slope Calib 100% trong không khí bão hòa).
- **Bù nhiệt đa chế độ**: 
  - **ATC (Automatic Temperature Compensation)**: Tự động bù nhiệt theo phương trình Nernst.
  - **MTC (Manual Temperature Compensation)**: Nhập nhiệt độ thủ công khi không có cảm biến.
  - **Bù tuyến tính $\alpha$**: Cài đặt hệ số bù nhiệt tuyến tính từ $-10.00\%$ đến $+10.00\%/^\circ C$ (quy đổi về chuẩn $25^\circ C$).

### 2.4. Trực quan hóa dữ liệu tại chỗ & Giả lập từ xa
- **Màn hình LCD đồ họa 128x64**: Hiển thị sắc nét dưới ánh sáng mặt trời ngoài trời.
- **3 Chế độ hiển thị Số**: pH toàn màn hình (Font đại 3x4), DO toàn màn hình (Font đại 3x4), hoặc DUAL (Hiển thị song song pH + DO font 2x2).
- **Biểu đồ thời gian thực (Real-time Trend Chart)**: Vẽ đồ thị diễn biến pH/DO 24 điểm liên tục ($\sim 48$ giây) trực tiếp trên LCD.
- **Bàn phím vật lý 5 nút**: Hỗ trợ phím tắt khởi động lại nhanh (nhấn giữ ENTER 7 giây).

### 2.5. Kết nối kép RS485 Modbus RTU linh hoạt & Đám mây Azure IoT Hub
- **Cổng Modbus RS485 kép (Dual Hardware RS485 Ports)**: Trang bị 2 cổng phần cứng RS485 độc lập hỗ trợ giao thức Modbus RTU tiêu chuẩn công nghiệp (Baudrate 2400 – 115200 bps, ID 1–247):
  - **Cổng 1 (Port 1 / External)**: Hiện tại được để trống (Standby), sẵn sàng cho việc kết nối mở rộng trong tương lai với hệ thống giám sát / điều khiển trung tâm (PLC, SCADA, màn hình HMI, Datalogger) hoặc kết nối bổ sung các cảm biến Modbus RTU khác (khi có nhu cầu tích hợp thêm).
  - **Cổng 2 (Port 2 / Sensor Port)**: Được phần mềm tích hợp mặc định cho việc đọc dữ liệu từ đầu dò DO quang học (KOG206). Cảm biến DO cần được đấu nối trực tiếp vào Cổng 2 để hệ thống nhận diện và cập nhật nồng độ DO chính xác.
  - **Khả năng kết nối nhiều thiết bị trên 1 cổng RS485 (Multi-drop Bus)**: Theo đặc tính kỹ thuật của chuẩn truyền thông RS485, **mỗi cổng RS485 hoàn toàn có thể kết nối song song nhiều thiết bị / cảm biến trên cùng một đường dây truyền thông** (lên tới 32 thiết bị tải tiêu chuẩn hoặc 247 địa chỉ Slave), miễn là thỏa mãn 2 điều kiện:
    1. Mỗi thiết bị / cảm biến trên cùng đường bus có một **Địa chỉ Modbus (Slave ID) duy nhất, không trùng lặp** (từ 1 đến 247).
    2. Tất cả các thiết bị kết nối chung trên đường bus đó phải được cài đặt **cùng Tốc độ Baud (Baudrate), Parity và Stop Bit**.
- **Web Config Portal không dây**: Thiết bị phát Wi-Fi AP (`MEBICO_ESP32_PH_xxxx`). Cho phép kỹ thuật viên cấu hình Wi-Fi, xem telemetry thời gian thực, và đặc biệt là **giả lập giao diện màn hình LCD & bàn phím điều khiển từ xa** trên trình duyệt điện thoại/laptop.
- **Azure IoT Hub Cloud Service**:
  - Gửi dữ liệu Telemetry chuẩn định dạng JSON về Cloud theo chu kỳ cài đặt (mặc định 1 giây - 10 giây).
  - Hỗ trợ **Direct Methods**: Truy vấn phiên bản (Code 502), bật/tắt đẩy dữ liệu (Code 503), hiệu chuẩn từ xa (Code 504), cấu hình tham số từ xa (Code 505).
  - Cập nhật phần mềm từ xa **OTA (Over-The-Air)** an toàn qua Cloud (Code 501).

### 2.6. Độ tin cậy cao & Lưu trữ an toàn
- **Lưu trữ kép NVS + FRAM**: Bộ nhớ Flash NVS lưu cấu hình hệ thống; chip **FRAM** (Ferroelectric RAM) lưu trữ dữ liệu nhật ký, lịch trình và trạng thái chẩn đoán với độ bền ghi xóa lên tới hàng tỷ lần.
- **Đồng hồ DS3231 + NTP**: Duy trì thời gian thực chính xác ngay cả khi mất điện nhờ pin RTC, tự động đồng bộ qua máy chủ NTP khi có mạng Wi-Fi.

---

## 3. THÔNG SỐ KỸ THUẬT CHI TIẾT (TECHNICAL SPECIFICATIONS)

### 3.1. Bảng thông số đo lường & Sai số cho phép

| Thông số đo | Phạm vi đo | Độ phân giải | Độ chính xác / Sai số đo | Công nghệ cảm biến / Giao diện |
| :--- | :--- | :--- | :--- | :--- |
| **pH** | $0.00 \div 14.00\text{ pH}$ | $0.01\text{ pH}$ | **$\pm 0.01\text{ pH}$** | Điện cực thủy tinh kết hợp ADC 24-bit CS1237 Sigma-Delta (PGA=1, 40Hz) |
| **DO (Oxy hòa tan)** | $0.00 \div 20.00\text{ mg/L}$ (ppm) | $0.01\text{ mg/L}$ | **$\pm 0.1\text{ mg/L}$** (hoặc $\pm 1\%$ độ bão hòa) | Cảm biến quang học (Luminescence/Optical DO Probe) qua Modbus RS485 |
| **Độ bão hòa DO** | $0.0 \div 200.0\%$ | $0.1\%$ | **$\pm 1.0\%$** | Tính toán tự động từ nồng độ DO và Nhiệt độ |
| **Nhiệt độ (Temp)** | $-20.0^\circ C \div +150.0^\circ C$<br/>($-4.0^\circ F \div +302.0^\circ F$) | $0.1^\circ C$ / $0.1^\circ F$ | **$\pm 0.1^\circ C$** | Đầu dò PT1000 RTD (Mạch cầu điện trở $R_{calib}=750\,\Omega$) / NTC 10K Beta 3950 |

---

### 3.2. Ngưỡng điện áp hiệu chuẩn pH tiêu chuẩn & Dải chấp nhận (OK Tolerance)

Trong hệ thống firmware MebiEco, dải điện áp vi phân ($mV$) thu được từ đầu dò pH được kiểm tra khắt khe qua **2 cấp bảo vệ kép** (Giao diện màn hình LCD & Thuật toán xử lý lõi) để đảm bảo chỉ lưu các phép đo chuẩn xác:

1. **Cấp 1: Giao diện LCD ([screen_menu.c](file:///d:/Study/STUdied/Mebi/Completed_Versions/pH_DO_Bac_Lieu/main/src/screen_menu.c#L1718-L1785))**: Hiển thị trạng thái `OK` và mở nút **LƯU** khi điện áp nằm trong dải hoạt động tối ưu tại chỗ.
2. **Cấp 2: Thuật toán lõi ([ph_temp.c](file:///d:/Study/STUdied/Mebi/Completed_Versions/pH_DO_Bac_Lieu/main/src/ph_temp.c#L384-L406))**: Kiểm tra giới hạn điện áp an toàn tuyệt đối trước khi ghi vào bộ nhớ NVS và tính toán độ dốc.

| Mốc chuẩn pH | Điện áp tiêu chuẩn | Dải OK trên Màn hình LCD (`screen_menu.c`) | Dải Giới hạn Lõi (`ph_temp.c`) | Ghi chú loại dung dịch |
| :---: | :---: | :---: | :---: | :--- |
| **pH 4.00** | $+177\text{ mV}$ | **$+140.0 \div +210.0\text{ mV}$** | **$+120.0 \div +240.0\text{ mV}$** | Dung dịch đệm Axit (Dùng cho Calib 2D & 3D) |
| **pH 6.86** | $+8\text{ mV}$ | **$-20.0 \div +35.0\text{ mV}$** | **$-60.0 \div +60.0\text{ mV}$** | Dung dịch đệm Trung tính NIST (Calib 3D) |
| **pH 7.00** | $0\text{ mV}$ | **$-30.0 \div +30.0\text{ mV}$** | **$-60.0 \div +60.0\text{ mV}$** | Dung dịch đệm Trung tính chuẩn (Calib 2D & 3D) |
| **pH 9.18** | $-129\text{ mV}$ | **$-160.0 \div -95.0\text{ mV}$** | **$-240.0 \div -100.0\text{ mV}$** | Dung dịch đệm Kiềm NIST (Calib 3D) |
| **pH 10.00** | $-177\text{ mV}$ | **$-210.0 \div -140.0\text{ mV}$** | **$-240.0 \div -100.0\text{ mV}$** | Dung dịch đệm Kiềm chuẩn Âu/Mỹ (Calib 3D) |

> **Lưu ý:** 
> - Nếu điện áp nằm ngoài **Dải OK Màn hình**, giao diện LCD hiển thị `ERR` và **ẩn nút LƯU**.
> - Đồng thời, hệ thống tự động kiểm tra độ nhạy (Slope efficiency) quy đổi ở $25^\circ C$. Nếu hiệu suất điện cực nằm ngoài dải **$70\% \div 115\%$** (tương ứng $41.41 \div 68.03\text{ mV/pH}$), thuật toán sẽ hủy kết quả hiệu chuẩn và báo lỗi điện cực bị lão hóa/hỏng hóc.

---

### 3.3. Thông số phần cứng & Truyền thông

| Hạng mục | Chi tiết kỹ thuật |
| :--- | :--- |
| **Bộ xử lý trung tâm** | ESP32-S3 Dual-Core 32-bit Xtensa LX7 @ 240 MHz, 8MB Flash, 512KB SRAM |
| **Màn hình hiển thị** | LCD đồ họa GMG12864-06D (128x64 pixel), Driver ST7565R (SPI 500 kHz, DMA transfer) |
| **Bộ nhớ mở rộng** | NVS Flash + FRAM I2C công nghiệp chống trôi dữ liệu khi mất điện |
| **Chuẩn truyền thông Serial** | 2 cổng RS485 Half-Duplex độc lập (Tích hợp chân DE chuyển mạch tự động/thủ công) |
| **Cấu hình Modbus RTU** | Baudrate: 2400, 4800, 9600, 19200, 38400, 57600, 115200 bps.<br/>Parity: None / Even / Odd.<br/>Stop bits: 1 hoặc 2 bit. Slave ID: $1 \div 247$ |
| **Wi-Fi & Đám mây** | Wi-Fi 802.11 b/g/n (2.4 GHz). Chuẩn bảo mật WPA2/WPA3.<br/>Giao thức Azure IoT Hub MQTT/TLS với SAS Token Authentication |
| **Thời gian thực (RTC)** | DS3231 Precision I2C RTC tích hợp Pin lưu trữ + Đồng bộ NTP tự động |
| **Bảo mật** | Mã PIN 4 chữ số bảo vệ menu (Mặc định `1234`), Xác thực Auth Token cho Azure Direct Methods |
| **Nguồn điện hoạt động** | $9 \div 24\text{V DC}$ (Tích hợp mạch bảo vệ đảo cực và quá áp) |

---

## 4. GIAO DIỆN NGƯỜI DÙNG & HỆ THỐNG MENU

### 4.1. Bố cục nút bấm & Thao tác nhanh
Thiết bị tích hợp 5 nút bấm cơ học phía trước:
- **`UP ▲` / `DOWN ▼`**: Di chuyển con trỏ menu / Tăng giảm giá trị cài đặt.
- **`RIGHT ►`**: Chuyển đổi giữa chế độ **Hiển thị Số** $\leftrightarrow$ **Biểu đồ thời gian thực** (ở Màn hình đo); Chuyển ô nhập liệu (trong Cài đặt giờ/mật khẩu).
- **`ESC`**: Quay lại trang trước / Hủy thao tác.
- **`ENTER`**: Vào Menu chính / Xác nhận lưu cài đặt.
- **`GIỮ ENTER 7 GIÂY`**: Khởi động lại thiết bị (Software Reboot) an toàn ở bất kỳ màn hình nào.

---

### 4.2. Cấu trúc cây Menu chính

```text
[MÀN HÌNH ĐO LƯỜNG CHÍNH] ── ENTER ──► [MENU CHÍNH]
                                          ├── 1. Cài Đặt Hệ Thống
                                          │    ├── 1.1 Ngôn Ngữ (Tiếng Việt / Tiếng Anh)
                                          │    ├── 1.2 Ngày Tháng (Định dạng & Chỉnh giờ thủ công)
                                          │    ├── 1.3 Cài Đặt Màn Hình (Độ tương phản 0-63, Tỷ số điện trở 0-7)
                                          │    └── 1.4 Thay Đổi Mật Khẩu (Mật khẩu 4 chữ số)
                                          ├── 2. Cài Đặt Hiển Thị
                                          │    ├── 2.1 Chế Độ pH (Font lớn 3x4)
                                          │    ├── 2.2 Chế Độ DO (Font lớn 3x4)
                                          │    └── 2.3 Chế Độ pH & DO (Font song song 2x2)
                                          ├── 3. Cài Đặt Modbus [Yêu cầu Mật khẩu]
                                          │    ├── 3.1 Cổng Modbus 1 (MR/PLC: Địa chỉ, Baud, Parity, Stopbit)
                                          │    └── 3.2 Cổng Modbus 2 (Cảm biến DO: Địa chỉ, Baud, Parity, Stopbit)
                                          └── 4. Cài Đặt Cảm Biến [Yêu cầu Mật khẩu]
                                               ├── 4.1 Cấu Hình Cảm Biến pH
                                               │    ├── 4.1.1 Hiệu Chuẩn pH (2 điểm / 3 điểm / Reset)
                                               │    ├── 4.1.2 Bộ Lọc Số (Thấp L / Vừa M / Cao H)
                                               │    ├── 4.1.3 Chế Độ Nhiệt Độ (ATC °C / MTC °C / ATC °F / MTC °F)
                                               │    ├── 4.1.4 Cài Đặt Nhiệt Độ (Nhập tay MTC / Bù lệch ATC Offset)
                                               │    └── 4.1.5 Bù Tuyến Tính T (Hệ số alpha %/°C)
                                               └── 4.2 Cấu Hình Cảm Biến DO
                                                    ├── 4.2.1 Hiệu Chuẩn DO (Điểm 0 / Độ dốc Slope / Nhiệt độ)
                                                    └── 4.2.2 Reset Hiệu Chuẩn DO (Khôi phục mặc định)
```

---

## 5. TÍNH NĂNG ĐÁM MÂY AZURE IOT HUB & WEB PORTAL

### 5.1. Gói dữ liệu Telemetry (JSON Payload mẫu)
Dữ liệu gửi từ MebiEco lên Cloud Azure IoT Hub chứa đầy đủ thông tin vận hành và chẩn đoán:

```json
{
  "payload": {
    "HostName": "MebiEco-IoTHub.azure-devices.net",
    "DeviceId": "MebiEco_pH_DO_BacLieu_01",
    "Code": 504,
    "TimeStamp": 1779878400,
    "SensorData": {
      "ph": 7.15,
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
      "ver": "1.2.0",
      "ota_part": "ota_0"
    }
  }
}
```

---

### 5.2. Các lệnh điều khiển từ xa qua Cloud (Azure Direct Methods)

| Mã lệnh (Code) | Tên lệnh | Chức năng | Tham số đầu vào (JSON) |
| :---: | :--- | :--- | :--- |
| **501** | `CMD_CODE_UPDATE_FIRMWARE` | Cập nhật firmware OTA từ xa | `{"Version": "1.2.1", "Url": "https://.../firmware.bin"}` |
| **502** | `CMD_CODE_ASK_VERSION` | Đọc phiên bản firmware hiện tại | *(Không tham số)* |
| **503** | `CMD_CODE_GET_PH` | Bật/tắt/đọc tức thời telemetry | `{"Action": "START", "IntervalMs": 2000}` hoặc `{"Action": "STOP"}` |
| **504** | `CMD_CODE_PH_CALIBRATE` | Hiệu chuẩn cảm biến pH từ xa | `{"Action": "CAL_7_2PT"}` / `{"TargetPH": 7.00, "CalType": 2}` / `{"Action": "RESET_PH_SENSOR"}` |
| **505** | `CMD_CODE_SET_DEVICE_CONFIG` | Cấu hình Wi-Fi, Bộ lọc, Azure | `{"Token": "...", "Target": "filter", "Level": 1}` hoặc `{"Target": "wifi", "SSID": "...", "Password": "..."}` |
| **506** | `CMD_CODE_DO_CALIBRATE` | Hiệu chuẩn & Cấu hình cảm biến DO từ xa | `{"Action": "CAL_DO_ZERO"}` / `{"Action": "CAL_DO_SLOPE"}` / `{"Action": "CORRECT_DO_TEMP", "Value": 25.0}` / `{"Action": "COMPENSATE_SALINITY", "Value": 15.0}` / `{"Action": "RESET_DO_SENSOR"}` |
| **507** | `CMD_CODE_REBOOT` | Khởi động lại thiết bị từ xa | *(Không tham số)* |
| **508** | `CMD_CODE_GET_FULL_TELEMETRY` | Truy vấn toàn bộ Telemetry & Chẩn đoán hệ thống | *(Không tham số)* (Trả về full JSON bao gồm pH, DO, Temp, RSSI, Free RAM, Uptime, Reset reason, OTA partition, pH Calib coefficients) |

---

### 5.3. Tính năng Cập nhật Firmware Từ xa (OTA) An toàn & Chống Brick

Hệ thống MebiEco tích hợp giải pháp nâng cấp phần mềm từ xa qua mạng (Over-The-Air -- OTA) chuẩn công nghiệp, cho phép cập nhật tính năng mới hoặc vá lỗi từ xa mà không cần can thiệp phần cứng tại ao nuôi:

- **Kiến trúc Phân vùng Kép (Dual-Bank OTA Architecture)**: Bảng phân vùng Flash 16MB (`partitions.csv`) gồm 2 phân vùng ứng dụng độc lập `ota_0` (4MB) và `ota_1` (4MB). Khi nạp firmware mới, dữ liệu được ghi vào phân vùng dự phòng. Bootloader chỉ chuyển đổi quyền ưu tiên boot sang phiên bản mới khi toàn bộ dữ liệu đã ghi thành công và kiểm tra checksum hoàn hảo. Nếu mất mạng hoặc mất điện mid-way, thiết bị tự động khôi phục phiên bản cũ hoạt động bình thường, **chống nguy cơ brick 100%**.
- **Cơ chế "Reboot-to-OTA" trong RAM sạch**: Khi nhận lệnh OTA (Code 501 `CMD_CODE_UPDATE_FIRMWARE` từ Azure Cloud hoặc Web Portal), hệ thống lưu URL và cờ `ota_pending` vào bộ nhớ NVS Flash, đếm lùi 1.5 giây để phản hồi xong kết quả HTTP/Direct Method về cho Server rồi tự động `esp_restart()`. Việc tải firmware thực hiện ngay khi vừa boot lúc **RAM hoàn toàn sạch** (chưa chạy TLS hay Web Server), kết hợp bộ đệm HTTP 8KB mở rộng giúp tăng tốc độ tải nạp gấp 4--8 lần.
- **Bảo mật HTTPS SSL/TLS & Ghi Log NVS**: Xác thực chứng chỉ SSL/TLS (`esp_crt_bundle_attach`), hỗ trợ nạp từ Azure Blob Storage với chữ ký SAS Token (`sig=`) hoặc Bearer Auth Token. Tiến trình và trạng thái nạp được lưu vào NVS Flash (`ota_res` = *success/failed*, `ota_err`) để báo cáo minh bạch cho Azure Cloud ở lần khởi động kế tiếp.

---

## 6. ỨNG DỤNG THỰC TẾ (MÔ HÌNH NUÔI TÔM BẠC LIÊU)

Hệ thống MebiEco tập trung chủ lực vào **Nuôi trồng thủy sản công nghệ cao**, đặc biệt tối ưu cho các mô hình nuôi tôm mật độ cao tại Bạc Liêu và Đồng bằng sông Cửu Long:
- **Giám sát liên tục các thông số "vàng"**: Theo dõi chặt chẽ biến động **pH** (ngăn ngừa hiện tượng sốc pH giữa ngày và đêm) và **Oxy hòa tan DO** (đảm bảo duy trì ngưỡng an toàn $> 4.0\text{ mg/L}$ cho tôm thẻ chân trắng, tôm sú nuôi mật độ cao).
- **Truyền dữ liệu không dây qua Azure IoT Hub**: Dữ liệu cảm biến được đóng gói chuẩn JSON và truyền trực tiếp lên nền tảng đám mây Azure IoT Hub theo thời gian thực với độ ổn định và bảo mật cao.
- **Ứng dụng (App) trực quan hóa & Vẽ đồ thị**: Dữ liệu từ Cloud được đồng bộ về App di động / Web Dashboard để vẽ đồ thị diễn biến pH, DO, Nhiệt độ trực quan, giúp chủ trang trại và kỹ sư dễ dàng theo dõi xu hướng chất lượng nước từ xa 24/7 và đưa ra quyết định xử lý kịp thời.

---

## 7. KẾT LUẬN

Hệ thống **MebiEco (ESP32-S3 pH/DO/Temp)** là giải pháp toàn diện, kết hợp hoàn hảo giữa **phần cứng đo lường công nghiệp độ chính xác cao**, **thuật toán xử lý tín hiệu lọc nhiễu 5 tầng**, và **hệ sinh thái phần mềm hiện đại (LCD + Web Portal + Cloud Azure)**. Đây là sự lựa chọn tối ưu cho các doanh nghiệp, trang trại thủy sản và nhà tích hợp hệ thống đang tìm kiếm một sản phẩm quan trắc chất lượng nước tin cậy, hiện đại và sẵn sàng cho cách mạng Công nghiệp 4.0.

---
*Tài liệu được trích xuất và tổng hợp tự động từ Mã nguồn Firmware & Hồ sơ Kỹ thuật MebiEco.*
