# ⚠️ FreeRTOS Cheatsheet: Cạm Bẫy `vTaskDelete()` Khi Task Đang Giữ Mutex Hoặc Socket

> **Bài học thực chiến (Lessons Learned)**: Không bao giờ được gọi `vTaskDelete(handle)` cưỡng bức một Task từ bên ngoài nếu Task đó có khả năng đang nắm giữ Mutex, Semaphore, hoặc đang trong quá trình giao tiếp Socket TCP/IP.

---

## 💣 1. Vấn Đề Cốt Lõi: Điều Gì Xảy Ra Khi Xóa Task Cưỡng Bức?

Trong FreeRTOS, hàm `vTaskDelete(TaskHandle_t xTaskToDelete)` hủy ngay lập tức Task được chỉ định. Tuy nhiên, **kernel của FreeRTOS KHÔNG tự động giải phóng tài nguyên** mà Task đó đang chiếm giữ.

### 🔴 Hậu quả 1: Khóa vĩnh viễn Mutex (Deadlock)
- **Cơ chế**: Mutex trong FreeRTOS có khái niệm *Task Ownership* (Quyền sở hữu). Chỉ duy nhất Task đã `xSemaphoreTake()` thành công mới có thể `xSemaphoreGive()` để trả Mutex.
- **Tịch thu vĩnh viễn**: Nếu Task B gọi `vTaskDelete(Task_A)` trong lúc Task A đang giữ `my_mutex`, thì Mutex đó **bị khóa vĩnh viễn (Locked Forever)**.
- **Hệ lụy**: Bất kỳ Task nào khác trong hệ thống sau đó cố gắng `xSemaphoreTake(my_mutex)` sẽ bị **treo vĩnh viễn (Hang / Deadlock)**.

### 🔴 Hậu quả 2: Rò rỉ Socket & Cạnh tranh tài nguyên Mạng (Network Socket Leak)
- **Cơ chế**: Khi một Task đang gọi các hàm socket nghẽn (Blocking call) như `connect()`, `recv()`, `send()`, hoặc đang thực thi các hàm trong thư viện TLS/MbedTLS... nếu bị hủy đột ngột:
  - File descriptor của Socket không được đóng (`close()` không được gọi).
  - Trạng thái kết nối TCP rơi vào trạng thái lơ lửng (*Half-Open TCP Connection*).
- **Hệ lụy**: Khi một Task khác (như OTA Client) khởi tạo kết nối HTTPS mới, driver Socket/lwIP của ESP-IDF bị kẹt ở hàm `connect()` hoặc `mbedtls_net_connect()` do cổng/buffer cũ vẫn chưa giải phóng.

### 🔴 Hậu quả 3: Idle Task không kịp thu hồi bộ nhớ Stack
- Trong FreeRTOS, khi gọi `vTaskDelete()`, bộ nhớ TCB và Stack của Task bị xóa **chưa được giải phóng ngay lập tức**, mà phải chờ **Task `Idle`** (Priority 0) được cấp CPU time để dọn dẹp.
- Nếu Task gọi lệnh xóa có độ ưu tiên cao (Priority 4, 5...) mà không nhường CPU (`vTaskDelay`), Task `Idle` sẽ không bao giờ chạy $\rightarrow$ **RAM không hề được giải phóng**.

---

## 🔍 2. Dấu Hiệu Nhận Biết Bẫy Này Trên Thực Tế

- ❌ Thiết bị ngẫu nhiên bị **treo cứng (Freeze)** ở các bước Cleanup, dọn dẹp tiến trình (ví dụ: *"Waiting for Azure disconnect..."*).
- ❌ Log dừng im lặng ở các hàm giao tiếp mạng (`connect()`, `esp_https_ota_begin()`).
- ❌ Lỗi **Stack Overflow** hoặc **Out of Memory** mặc dù đã gọi lệnh "xóa Task".
- ❌ Hệ thống bị **Watchdog Reset (WDT)** do một Task khác đứng chờ Mutex quá lâu.

---

## 🛠️ 3. Các Giải Pháp Xử Lý Chuẩn (Best Practices)

---

### 🟢 GIẢI PHÁP A (Ưu tiên hàng đầu cho các tác vụ lớn như OTA): Reboot-to-Clean-State

> **Triết lý**: Đánh đổi thêm 1 lần khởi động lại ngắn (~1–2s) để nhận lại **100% độ tin cậy tuyệt đối** và RAM hoàn toàn sạch.

#### ⚙️ Cơ chế hoạt động:
1. Khi nhận yêu cầu OTA (từ Cloud hoặc Web Portal): Ghi URL firmware và cờ `ota_pending = 1` vào **NVS / RTC RAM**.
2. Phản hồi ACK thành công về cho Cloud/WebClient, sau đó hẹn giờ 1.5s gọi `esp_restart()`.
3. **Khi chip vừa khởi động lại (Lần 1)**: Vừa có Wi-Fi, chưa bật Azure IoT Hub, chưa bật Sensor Task, RAM tự do tối đa (**~186KB Free Heap**):
   - Đọc NVS thấy `ota_pending == 1` $\rightarrow$ Xóa cờ ngay và tiến hành **Download & Flash Firmware**.
4. Nạp thành công/thất bại $\rightarrow$ Ghi `ota_res` vào NVS và `esp_restart()` **(Lần 2)** vào hệ thống bình thường.

#### 💻 Code mẫu minh họa:
```c
// 1. Khi nhận lệnh OTA từ Cloud/Web
void Trigger_OTA_Event(const char *url) {
    Nvs_Write_String("ota_url", url);
    Nvs_Write_String("ota_pending", "1");
    
    // Hẹn giờ 1.5s restart để gửi xong gói phản hồi ACK
    esp_restart_delayed(1500); 
}

// 2. Chạy ngay khi vừa có Wi-Fi lúc bootup (RAM còn sạch 100%)
bool Check_And_Run_OTA_Pending(void) {
    char pending[8] = {0};
    if (!Nvs_Read_String("ota_pending", pending) || strcmp(pending, "1") != 0) {
        return false;
    }
    
    Nvs_Write_String("ota_pending", "0"); // Xóa cờ tránh bootloop
    
    char url[512] = {0};
    Nvs_Read_String("ota_url", url);
    
    // Tải và nạp firmware trong môi trường RAM sạch tuyệt đối
    esp_err_t ret = update_firmware(url); 
    if (ret == ESP_OK) {
        Nvs_Write_String("ota_res", "success");
    } else {
        Nvs_Write_String("ota_res", "failed");
    }
    
    esp_restart(); // Reboot lần 2 vào hệ thống
    return true;
}
```

#### 🎯 Đánh giá Giải Pháp A:
- **Đánh đổi**: Thiết bị khởi động lại 2 lần (Lần 1 để nạp, Lần 2 để chạy FW mới).
- **Đạt được**: 
  - Triệt tiêu **100% rủi ro Treo/Deadlock**, Rò rỉ Socket hay Phân mảnh RAM.
  - Code gọn nhẹ, cực kỳ dễ bảo trì, không cần dọn dẹp Task/Queue rắc rối.

---

### 🟢 GIẢI PHÁP B: Graceful Shutdown (Tự Giải Phóng Tài Nguyên Bằng Cờ Tín Hiệu)

> **Triết lý**: Áp dụng cho các Task phụ ngắn hạn khi không muốn reset toàn bộ phần cứng. Không bao giờ xóa Task từ bên ngoài, chỉ phát tín hiệu để Task **TỰ THOÁT**.

#### ⚙️ Cơ chế hoạt động:
- Tạo một cờ ngắt toàn cục `volatile bool s_stop_requested = false`.
- Task con định kỳ kiểm tra cờ này. Nếu thấy `true`, Task sẽ **chủ động nhả Mutex**, đóng Socket TLS chuẩn, giải phóng bộ nhớ cá nhân, rồi tự gọi `vTaskDelete(NULL)`.

#### 💻 Code mẫu minh họa:
```c
static volatile bool s_stop_requested = false;

void my_worker_task(void *pvParameters) {
    while (!s_stop_requested) {
        if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // Thực thi công việc...
            
            // Luôn nhả Mutex trước khi lặp tiếp
            xSemaphoreGive(xMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // Dọn dẹp tài nguyên cá nhân trước khi tự kết thúc
    close(my_socket);
    vTaskDelete(NULL); // Task TỰ XÓA CHÍNH NÓ (An toàn tuyệt đối!)
}

void stop_worker_task(void) {
    s_stop_requested = true; // Bật cờ yêu cầu dừng từ bên ngoài
}
```

#### 🎯 Đánh giá Giải Pháp B:
- **Đánh đổi**: Code phức tạp hơn, phải chèn cờ dừng ở nhiều nơi và phải chờ hết timeout của các hàm nghẽn (Blocking calls).
- **Đạt được**: Ngắt tiến trình êm ái mà không cần khởi động lại chip.

---

## 📌 4. Bảng Tóm Tắt Nhanh (Cheatsheet Summary)

| Phương pháp | Mức độ an toàn | Khi nào nên dùng? | Đánh đổi |
| :--- | :---: | :--- | :--- |
| **`vTaskDelete(handle)` từ bên ngoài** | 🔴 **CỰC NGUY HIỂM** | ❌ **Không bao giờ dùng** cho Task có Mutex/Socket. | Dễ gây Deadlock, kẹt Socket, treo chip. |
| **Giải pháp A: Reboot-to-Clean-State** | ⭐️ **TUYỆT ĐỐI (100%)** | Các tác vụ lớn, tốn RAM & đòi hỏi độ ổn định tuyệt đối như **OTA**. | Đánh đổi 1 lần reboot ngắn (~1-2s). |
| **Giải pháp B: Graceful Shutdown (Soft Flag)** | 🟢 **AN TOÀN** | Các Task phụ, tác vụ nhỏ không muốn reset phần cứng. | Code phức tạp hơn, tốn thời gian chờ timeout. |
| **`vTaskDelete(NULL)` (Tự xóa chính mình)** | 🟢 **AN TOÀN** | Dùng ở cuối hàm Task sau khi đã tự giải phóng hết Mutex/Socket. | Không có. |