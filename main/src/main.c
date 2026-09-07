#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <inttypes.h>
#include <stdio.h>

#include "cs1237.h"
#include "do_sensor.h"
#include "ds3231.h"
#include "filter.h"
#include "ph_temp.h"
#include "screen_disp.h"
#include "user_azure.h"
#include "user_fram.h"
#include "user_http_server.h"
#include "user_system.h"
#include "user_time.h"
#include "user_ota.h"
#include "wifi_config_manager.h"
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

static const char *TAG = "PH_TEMP_SYSTEM";

MovingAverage_t ph_filter;
MovingAverage_t temp_filter;

TaskHandle_t Azure_Task_Handle = NULL;
TaskHandle_t Http_Server_Task_Handle = NULL;
TaskHandle_t Timer_Task_Handle = NULL;
TaskHandle_t OTA_Task_Handle = NULL;

static void on_do_reading(const do_sensor_reading_t *reading, void *user_ctx) {
  (void)user_ctx;
  if (!reading->valid) {
    ESP_LOGW("DO_SENSOR", "callback: loi code=%d", reading->error_code);
    return;
  }
  static uint32_t last_do_log_time = 0;
  static bool first_do_log = true;
  uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
  if (first_do_log || (now - last_do_log_time >= 5000)) {
    last_do_log_time = now;
    first_do_log = false;
    ESP_LOGI("DO_SENSOR", ">>> DO=%.2f mg/L | sat=%.1f%% | temp=%.1f C",
             reading->do_mg_l, reading->saturation_pct, reading->temp_c);
  }
}

static void ph_temp_sensor_task(void *pvParameters) {
  ESP_LOGI(TAG, "ph_temp_sensor_task bat dau chay trên Core 1");

  // Đọc mẫu ban đầu để nạp sẵn giá trị cho bộ lọc trung vị (tránh lệch/trễ lúc khởi động)
  int32_t init_temp = 0;
  int32_t init_ph = 0;
  read_cs1237_raw(TEMP_SCLK_PIN, TEMP_DATA_PIN, &init_temp);
  read_cs1237_raw(PH_SCLK_PIN, PH_DATA_PIN, &init_ph);

  float init_temp_c = calculate_temperature(init_temp, true) + g_temp_offset;
  ewma_init(&temp_ewma_filter, init_temp_c, 0.10f);

  float init_v_mv = 0;
  bool init_valid = true;
  float init_ph_val = calculate_ph_with_atc_calibrated(&ph_cal, init_ph, init_temp_c, &init_v_mv, &init_valid);
  kalman1d_init(&ph_kalman_filter, init_ph_val, 0.001f, 0.05f);

  init_spike_filter(&temp_spike_filter, init_temp, SPIKE_TEMP_MAX_DELTA_RAW, SPIKE_MAX_ALLOWED_COUNT);
  init_spike_filter(&ph_spike_filter, init_ph, SPIKE_PH_MAX_DELTA_RAW, SPIKE_MAX_ALLOWED_COUNT);

  init_median_filter(&temp_median_filter, init_temp);
  init_median_filter(&ph_median_filter, init_ph);

  while (1) {
    // 1. KÊNH NHIỆT ĐỘ
    int32_t temp_raw = 0;
    int32_t temp_med = 0;
    int32_t temp_filtered = 0;
    float current_temp = 25.0f;
    bool temp_valid = true;
    bool temp_read_ok = true;

    if (g_temp_mode == TEMP_MODE_MTC_C || g_temp_mode == TEMP_MODE_MTC_F) {
        current_temp = g_manual_temp;
        temp_valid = true;
    } else {
        temp_read_ok = read_cs1237_raw(TEMP_SCLK_PIN, TEMP_DATA_PIN, &temp_raw);
        if (!temp_read_ok) {
            temp_valid = false;
            current_temp = 25.0f; // Bù nhiệt mặc định 25C cho thuật toán pH nếu đo bị lỗi
        } else {
            bool temp_step_detected = false;
            int32_t temp_spiked = apply_spike_filter(&temp_spike_filter, temp_raw, &temp_step_detected);
            temp_med = apply_median_filter(&temp_median_filter, temp_spiked);
            temp_filtered = apply_moving_average(&temp_filter, temp_med);
            float measured_temp = calculate_temperature(temp_filtered, true) + g_temp_offset; // true = PT1000
            if (temp_step_detected) {
                reset_median_filter_val(&temp_median_filter, temp_spiked);
                reset_moving_average_val(&temp_filter, temp_spiked);
                ewma_reset(&temp_ewma_filter, measured_temp);
                ESP_LOGI(TAG, "Phat hien buoc nhay nhiet do thuc te! Flush buffer voi ADC raw=%" PRId32, temp_spiked);
            }
            if (measured_temp < 0.0f || measured_temp > 60.0f) {
                temp_valid = false;
                current_temp = 25.0f; // Bù nhiệt mặc định 25C cho thuật toán pH nếu đo bị lỗi
            } else {
                temp_valid = true;
                current_temp = ewma_update(&temp_ewma_filter, measured_temp);
            }
        }
    }

    // 2. KÊNH pH
    int32_t ph_raw = 0;
    bool ph_read_ok = read_cs1237_raw(PH_SCLK_PIN, PH_DATA_PIN, &ph_raw);
    int32_t ph_med = 0;
    int32_t ph_filtered = 0;
    float v_probe_mv = 0;
    bool ph_valid = true;
    float current_ph = 7.00f;

    if (!ph_read_ok) {
        ph_valid = false;
        current_ph = 7.00f;
    } else {
        bool ph_step_detected = false;
        int32_t ph_spiked = apply_spike_filter(&ph_spike_filter, ph_raw, &ph_step_detected);
        ph_med = apply_median_filter(&ph_median_filter, ph_spiked);
        ph_filtered = apply_moving_average(&ph_filter, ph_med);
        float calculated_ph = calculate_ph_with_atc_calibrated(
            &ph_cal, ph_filtered, current_temp, &v_probe_mv, &ph_valid);
        if (ph_step_detected) {
            reset_median_filter_val(&ph_median_filter, ph_spiked);
            reset_moving_average_val(&ph_filter, ph_spiked);
            kalman1d_reset(&ph_kalman_filter, calculated_ph);
            ESP_LOGI(TAG, "Phat hien buoc nhay pH thuc te! Flush buffer voi ADC raw=%" PRId32, ph_spiked);
        }
        if (ph_valid) {
            current_ph = kalman1d_update(&ph_kalman_filter, calculated_ph);
        } else {
            current_ph = calculated_ph;
        }
    }

    // --- CƠ CHẾ TỰ PHỤC HỒI CS1237 (HARDWARE WATCHDOG) ---
    static uint32_t s_cs1237_fail_count = 0;
    bool cs1237_both_ok = (g_temp_mode == TEMP_MODE_MTC_C || g_temp_mode == TEMP_MODE_MTC_F) ? ph_read_ok : (temp_read_ok && ph_read_ok);
    if (!cs1237_both_ok) {
        s_cs1237_fail_count++;
        if (s_cs1237_fail_count >= 10) {
            ESP_LOGW(TAG, "CS1237 mat ket noi / ket giao tiep lien tiep %lu lan -> Tien hanh khoi tao lai GPIO va ghi lai thanh ghi cau hinh CS1237...", (unsigned long)s_cs1237_fail_count);
            init_system_gpios();
            vTaskDelay(pdMS_TO_TICKS(20));
            write_cs1237_config(PH_SCLK_PIN, PH_DATA_PIN, CS1237_CFG_40HZ_PGA1_CHA);
            write_cs1237_config(TEMP_SCLK_PIN, TEMP_DATA_PIN, CS1237_CFG_40HZ_PGA1_CHA);
            s_cs1237_fail_count = 0;
        }
    } else {
        s_cs1237_fail_count = 0;
    }

    // Tính v_diff cho cả raw, med và filtered (đổi ra mV)
    float v_diff_raw_mv = (((float)ph_raw * V_REF_ADC) / ADC_DIVISOR) * 1000.0f;
    float v_diff_med_mv = (((float)ph_med * V_REF_ADC) / ADC_DIVISOR) * 1000.0f;
    float v_diff_filt_mv =
        (((float)ph_filtered * V_REF_ADC) / ADC_DIVISOR) * 1000.0f;

    // =========================================================================
    // CODE MÔ PHỎNG SENSOR (TỰ ĐỘNG THAY ĐỔI 5 GIÁ TRỊ BAO GỒM CẢ CÁC GIÁ TRỊ LỖI)
    // Để chuyển sang chạy thực tế: Đổi '#if 1' bên dưới thành '#if 0' hoặc comment lại toàn bộ khối này
    // =========================================================================
#if 1
    /* Chuỗi mô phỏng 5 bước (pH ~ 7.50, Temp ~ 30.2°C):
     * Tự động tính toán v_probe_mv hợp lý theo phương trình Nernst ở nhiệt độ mô phỏng
     * Với pH 7.50 & 30°C: Nernst Slope ≈ -60.18 mV/pH -> v_probe_mv ≈ -30.09 mV
     */
    static const float s_sim_ph[5]   = { 8.01f,  8.02f,  8.32f,  8.53f,  8.54f };
    static const float s_sim_temp[5] = {30.30f, 30.10f, 30.20f, 30.40f, 30.20f };

    static size_t s_sim_idx = 0;

    current_ph = s_sim_ph[s_sim_idx];
    ph_valid = (current_ph >= 0.0f && current_ph <= 14.0f);

    current_temp = s_sim_temp[s_sim_idx];
    temp_valid = (current_temp >= 0.0f && current_temp <= 60.0f);

    // Tính toán v_probe_mv hợp lý theo phương trình Nernst phụ thuộc pH và Nhiệt độ mô phỏng
    float nernst_slope = -59.16f * ((current_temp + 273.15f) / 298.15f);
    v_probe_mv = (current_ph - 7.00f) * nernst_slope + ph_cal.ph7_voltage_mv;

    s_sim_idx = (s_sim_idx + 1) % 5;
#endif
    // =========================================================================

    // 3. IN KẾT QUẢ
    static uint32_t last_ph_log_time = 0;
    static bool first_ph_log = true;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (first_ph_log || (now - last_ph_log_time >= 5000)) {
      last_ph_log_time = now;
      first_ph_log = false;
      ESP_LOGI(TAG,
               "Nhiệt độ: %.2f C (valid:%d) | ADC Temp: %" PRId32 " | ADC pH Raw: %" PRId32
               " (v_diff: %.2f mV) | Med: %" PRId32 " (v_diff: %.2f mV) | Filt: %" PRId32 " (v_diff: %.2f mV)"
               " | V_Probe: %.2f mV | pH (ATC): %.2f (valid:%d)",
               current_temp, temp_valid, temp_filtered, ph_raw, v_diff_raw_mv, ph_med, v_diff_med_mv, ph_filtered,
               v_diff_filt_mv, v_probe_mv, current_ph, ph_valid);
    }

    // Cập nhật giá trị pH và Nhiệt độ thực tế lên màn hình hiển thị
    screen_update_values(current_ph, current_temp);

    // Cập nhật trạng thái cảm biến toàn cục
    Update_Sensor_Measurements(current_ph, ph_valid, current_temp, temp_valid, v_probe_mv);

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

static void system_startup_task(void *pvParameters) {
  // --- Khởi tạo Wifi Manager ---
  wifi_config_manager_init();

  // --- Tạo Http Server Task (luôn tạo để vào được Web Portal cấu hình) ---
  if (xTaskCreatePinnedToCore((TaskFunction_t)User_Http_Server_Task,
                              "Http_Server_Task", 6144, NULL, 5,
                              &Http_Server_Task_Handle, 0) == pdPASS) {
    ESP_LOGI(TAG, "Create Http_Server_Task successfully");
  } else {
    ESP_LOGE(TAG, "Create Http_Server_Task failed!");
  }

  // --- Đợi WiFi kết nối thành công ---
  while (!Sys_Info.isWifiConnected) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  vTaskDelay(pdMS_TO_TICKS(2000));

  // --- KÍCH HOẠT REBOOT-TO-OTA (NẾU CÓ ĐƠN HÀNG OTA PENDING TRONG NVS) ---
  // Tiến hành nạp Firmware ngay khi vừa có Wi-Fi, RAM trống tuyệt đối (chưa khởi tạo Azure / NTP)
  User_Ota_Check_And_Run_Pending();

  // --- Tạo Time Task (Đồng bộ thời gian NTP) ---
  if (xTaskCreatePinnedToCore((TaskFunction_t)User_Time_Task, "Time_Task", 4096,
                              NULL, 5, &Timer_Task_Handle, 1) == pdPASS) {
    ESP_LOGI(TAG, "Create Time_Task successfully");
  } else {
    ESP_LOGE(TAG, "Create Time_Task failed!");
  }

  // --- Đợi đồng bộ thời gian NTP thành công ---
  while (!Sys_Info.isTimeSync) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  vTaskDelay(pdMS_TO_TICKS(2000));

  // --- Tạo Azure Task (Kết nối Cloud) ---
  if (xTaskCreatePinnedToCore((TaskFunction_t)User_Azure_Task, "Azure_Task",
                              6144, NULL, 5, &Azure_Task_Handle, 0) == pdPASS) {
    ESP_LOGI(TAG, "Create Azure_Task successfully");
  } else {
    ESP_LOGE(TAG, "Create Azure_Task failed!");
  }

  vTaskDelete(NULL);
}

void app_main(void) {
  init_system_gpios();

  /* Fix mui gio ngay tu dau (UTC+7 = "UTC-7" theo POSIX), khong phu thuoc RTC.
   * Neu khong, khi RTC loi/dung thi TZ chua duoc set -> dong ho hien sai UTC. */
  setenv("TZ", "UTC-7", 1);
  tzset();

  init_moving_average(&temp_filter);
  init_moving_average(&ph_filter);

  vTaskDelay(pdMS_TO_TICKS(500));

  // Cấu hình cả hai chip: 40Hz, PGA = 1, Kênh A (0x10)
  write_cs1237_config(PH_SCLK_PIN, PH_DATA_PIN, CS1237_CFG_40HZ_PGA1_CHA);
  vTaskDelay(pdMS_TO_TICKS(50));
  write_cs1237_config(TEMP_SCLK_PIN, TEMP_DATA_PIN, CS1237_CFG_40HZ_PGA1_CHA);
  vTaskDelay(pdMS_TO_TICKS(300));
  uint8_t current_cfg = read_cs1237_config(TEMP_SCLK_PIN, TEMP_DATA_PIN);
  uint8_t current_cfg_ph = read_cs1237_config(PH_SCLK_PIN, PH_DATA_PIN);

  ESP_LOGI("CS1237_DEBUG", "Cấu hình hiện tại trong chip là: 0x%02X",
           current_cfg);
  ESP_LOGI("CS1237_DEBUG", "Cấu hình hiện tại trong chip ph la: 0x%02X",
           current_cfg_ph);

  // --- KHỞI TẠO RTC DS3231 ---
  if (ds3231_init(GPIO_NUM_3, GPIO_NUM_20) == ESP_OK) {
    bool stopped = false;
    if (ds3231_check_oscillator(&stopped) == ESP_OK && !stopped) {
      struct tm rtc_time = {0};
      if (ds3231_get_time(&rtc_time) == ESP_OK) {
        setenv("TZ", "UTC-7", 1);
        tzset();
        struct timeval tv = {.tv_sec = mktime(&rtc_time), .tv_usec = 0};
        if (tv.tv_sec != -1) {
          settimeofday(&tv, NULL);
          char timeBuf[64];
          strftime(timeBuf, sizeof(timeBuf), "%c", &rtc_time);
          ESP_LOGI(TAG, "Đã đồng bộ thời gian từ RTC DS3231: %s", timeBuf);
        }
      }
    } else {
      ESP_LOGW(TAG, "RTC Oscillator bị dừng hoặc lỗi, thời gian chưa tin cậy!");
    }
  } else {
    ESP_LOGE(TAG, "Khởi tạo RTC DS3231 thất bại!");
  }

  // --- KHỞI TẠO MÀN HÌNH LCD ---
  if (LCD_Init() == LCD_OK) {
    LCD_Start_Task();
  } else {
    ESP_LOGE(TAG, "Khởi tạo LCD thất bại!");
  }

  // --- Khởi tạo NVS Flash ---
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // --- Tải cấu hình hiệu chuẩn ---
  Load_Calibration_From_Storage();

  // --- Khởi tạo User System ---
  User_System_Init();

  // --- Tải cấu hình Modbus ---
  do_sensor_load_settings();

  // --- Khởi tạo cảm biến DO (Modbus RS485) ---
  do_sensor_config_t do_cfg = DO_SENSOR_CONFIG_DEFAULT();
  do_cfg.uart_port = UART_NUM_1;
  do_cfg.pin_tx = CONFIG_MB_PORT2_PIN_TX;
  do_cfg.pin_rx = CONFIG_MB_PORT2_PIN_RX;
  do_cfg.pin_de = CONFIG_MB_PORT2_PIN_DE;
  do_cfg.slave_id = g_mb2_addr;
  do_cfg.baud_rate = g_mb2_baud;
  do_cfg.parity = (g_mb2_parity == 1) ? UART_PARITY_EVEN : ((g_mb2_parity == 2) ? UART_PARITY_ODD : UART_PARITY_DISABLE);
  do_cfg.stop_bits = (g_mb2_stop == 2) ? UART_STOP_BITS_2 : UART_STOP_BITS_1;
  do_cfg.rs485_mode = DO_SENSOR_RS485_MANUAL_DE;
  do_cfg.poll_interval_ms = 500;
  do_cfg.debug = false;

  ESP_LOGI(TAG, "Doc DO moi %lu giay/lan",
           (unsigned long)do_cfg.poll_interval_ms / 1000);
  esp_err_t do_err = do_sensor_init(&do_cfg);
  if (do_err == ESP_OK) {
    do_sensor_start(on_do_reading, NULL);
  } else {
    ESP_LOGE(TAG, "Khoi tao cam bien DO that bai: %s", esp_err_to_name(do_err));
  }

  // --- Khởi chạy tiến trình khởi tạo mạng tuần tự ---
  xTaskCreatePinnedToCore(system_startup_task, "system_startup_task", 9216,
                          NULL, 5, NULL, 0);


  // --- Khởi chạy task đọc cảm biến pH & Nhiệt độ (CS1237) ---
  xTaskCreatePinnedToCore(ph_temp_sensor_task, "ph_temp_sensor_task", 4096,
                          NULL, 4, NULL, 1);

  // --- Khởi chạy task ghi dữ liệu môi trường vào FRAM định kỳ 1 phút/lần ---
  xTaskCreatePinnedToCore((TaskFunction_t)User_Fram_Task, "User_Fram_Task", 3072,
                          NULL, 3, NULL, 1);

  ESP_LOGI(TAG, "Da khoi tao he thong giam sat pH, Nhiet do & DO hoan chinh.");

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}
