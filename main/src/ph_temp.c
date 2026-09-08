#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "ph_temp.h"
#include "user_storage.h"

static const char *TAG = "PH_TEMP_ALGO";

temp_mode_t g_temp_mode = TEMP_MODE_ATC_C;
float g_manual_temp = 25.0f;
float g_temp_alpha = 0.0f;
float g_temp_offset = 0.0f;

// Đối tượng hiệu chuẩn toàn cục dùng chung cho hệ thống
PhCalibration_t ph_cal = {
    .ph7_voltage_mv = 1.63f,
    .ph7_temp_c = 30.97f,
    .ph4_voltage_mv = 176.80f,
    .ph4_temp_c = 30.89f,
    .ph10_voltage_mv = -177.0f,
    .ph10_temp_c = 25.0f,
    .ph10_target = 10.00f,
    .slope_norm = -5.207f,
    .slope_high = -5.207f,
    .u7 = 0.00536f,
    .cal_type = 2,
    .is_calibrated = true
};

// Cấu trúc trạng thái cảm biến toàn cục
static PH_Temp_Sensor_Status_t s_sensor_status = {0};

void update_ph_calibration(PhCalibration_t *cal)
{
    float t7_k = cal->ph7_temp_c + 273.15f;
    float t4_k = cal->ph4_temp_c + 273.15f;

    cal->u7 = cal->ph7_voltage_mv / t7_k;
    float u4 = cal->ph4_voltage_mv / t4_k;

    if (cal->cal_type == 3) {
        float t10_k = cal->ph10_temp_c + 273.15f;
        float u10 = cal->ph10_voltage_mv / t10_k;

        float mid_target = (cal->ph10_target < 9.5f) ? 6.86f : 7.00f;

        float du_low = fabsf(cal->u7 - u4);
        float du_high = fabsf(cal->u7 - u10);

        bool low_ok = du_low > 0.1f;
        bool high_ok = du_high > 0.1f;

        if (low_ok && high_ok) {
            cal->slope_norm = (mid_target - 4.00f) / (cal->u7 - u4);
            cal->slope_high = (cal->ph10_target - mid_target) / (u10 - cal->u7);

            // Tính độ nhạy quy đổi về mV/pH ở 25°C (298.15K)
            float sens_low = (du_low * 298.15f) / fabsf(mid_target - 4.00f);
            float sens_high = (du_high * 298.15f) / fabsf(cal->ph10_target - mid_target);

            float eff_low = (sens_low / 59.16f) * 100.0f;
            float eff_high = (sens_high / 59.16f) * 100.0f;

            bool low_slope_ok = (sens_low >= 41.41f && sens_low <= 68.03f);
            bool high_slope_ok = (sens_high >= 41.41f && sens_high <= 68.03f);

            if (low_slope_ok && high_slope_ok) {
                cal->is_calibrated = true;
                ESP_LOGI(TAG, "Hiệu chuẩn 3 điểm thành công! Sens_low: %.2f mV/pH (%.1f%%), Sens_high: %.2f mV/pH (%.1f%%), U7: %.4f",
                         sens_low, eff_low, sens_high, eff_high, cal->u7);
            } else {
                cal->is_calibrated = false;
                ESP_LOGE(TAG, "Lỗi hiệu chuẩn 3 điểm: Độ dốc Slope ngoài dải 70%%-115%%! Sens_low: %.2f mV/pH (%.1f%%), Sens_high: %.2f mV/pH (%.1f%%)",
                         sens_low, eff_low, sens_high, eff_high);
            }
        } else {
            cal->is_calibrated = false;
            ESP_LOGE(TAG, "Lỗi hiệu chuẩn 3 điểm: Điện áp các dung dịch quá gần nhau!");
        }
    } else { // 2-point
        float du_low = fabsf(cal->u7 - u4);
        if (du_low > 0.1f) {
            cal->slope_norm = (7.00f - 4.00f) / (cal->u7 - u4);
            cal->slope_high = cal->slope_norm;

            // Tính độ nhạy quy đổi về mV/pH ở 25°C (298.15K)
            float sens_low = (du_low * 298.15f) / 3.00f;
            float eff_low = (sens_low / 59.16f) * 100.0f;

            if (sens_low >= 41.41f && sens_low <= 68.03f) {
                cal->is_calibrated = true;
                ESP_LOGI(TAG, "Hiệu chuẩn 2 điểm thành công! Sens: %.2f mV/pH (Hiệu suất: %.1f%%), U7: %.4f", sens_low, eff_low, cal->u7);
            } else {
                cal->is_calibrated = false;
                ESP_LOGE(TAG, "Lỗi hiệu chuẩn 2 điểm: Độ dốc Slope ngoài dải 70%%-115%%! Sens: %.2f mV/pH (Hiệu suất: %.1f%%)", sens_low, eff_low);
            }
        } else {
            cal->is_calibrated = false;
            ESP_LOGE(TAG, "Lỗi hiệu chuẩn 2 điểm: Điện áp các dung dịch quá gần nhau!");
        }
    }
}

float calculate_temperature(int32_t raw_adc, bool is_pt1000)
{
    float v_diff = ((float)raw_adc * V_REF_ADC) / ADC_DIVISOR;

    // Phòng ngừa lỗi chia cho 0 hoặc giá trị quá nhỏ
    if (fabsf(v_diff) < 1e-6f) {
        v_diff = 1e-6f;
    }

    if (is_pt1000) {
        float ratio = V_REF_BRIDGE  / v_diff;
        if (ratio < 1.0f) {
            ratio = 1.0f; // Tránh điện trở âm bất thường
        }
        float r_pt1000 = R_CALIB * (ratio - 1.0f);
        return (r_pt1000 - 1000.0f) / 3.9083f;
    } else {
        float v_ntc = V_REF_BRIDGE  + v_diff;
        if (v_ntc <= 0.0f) {
            v_ntc = 1e-6f;
        }

        float ratio = V_REF_BRIDGE  / v_ntc;
        if (ratio <= 1.0001f) {
            ratio = 1.0001f; // Tránh lấy log của số âm hoặc bằng 0
        }

        float r_ntc = 750.0f / (ratio - 1.0f);
        float steinhart = r_ntc / NTC_NOMINAL_RESISTANCE;
        steinhart = logf(steinhart);
        steinhart /= NTC_BETA;
        steinhart += 1.0f / (NTC_T0_KELVIN);
        steinhart = 1.0f / steinhart;
        steinhart -= 273.15f;
        return steinhart;
    }
}

float calculate_ph_with_atc_calibrated(PhCalibration_t *cal, int32_t raw_adc, float temp_c, float *out_v_probe_mv, bool *out_ph_valid)
{
    float v_diff = ((float)raw_adc * V_REF_ADC) / ADC_DIVISOR;
    float v_probe =  v_diff * 2.0f; // Giải mã điện áp đầu dò thực tế (Khử offset và bù hệ số suy hao 0.5 của Op-Amp)

    float v_probe_mv = v_probe * 1000.0f;

    if (out_v_probe_mv) {
        *out_v_probe_mv = v_probe_mv;
    }

    // Đề phòng lỗi đo nhiệt độ bất thường
    if (temp_c < -40.0f || temp_c > 150.0f) {
        temp_c = 25.0f;
    }

    float ph_val;

    // Nếu hệ thống chưa được hiệu chuẩn thực tế: Fallback về công thức tuyến tính lý thuyết
    if (!cal->is_calibrated) {
        float slope_at_25 = -59.16f; // Mặc định lý thuyết ở 25C (298.15K)
        if (fabsf(cal->ph4_voltage_mv - cal->ph7_voltage_mv) > 1.0f) {
            slope_at_25 = (cal->ph4_voltage_mv - cal->ph7_voltage_mv) / (4.00f - 7.00f);
        }
        float slope_at_temp = slope_at_25 * ((temp_c + 273.15f) / 298.15f);
        ph_val = 7.00f + (v_probe_mv / slope_at_temp);
    } else {
        float temp_k = temp_c + 273.15f;
        float u_probe = v_probe_mv / temp_k;

        if (cal->cal_type == 3) {
            float mid_target = (cal->ph10_target < 9.5f) ? 6.86f : 7.00f;
            if (u_probe > cal->u7) { // pH <= mid_target (Axit)
                ph_val = mid_target + (u_probe - cal->u7) * cal->slope_norm;
            } else { // pH > mid_target (Kiềm)
                ph_val = mid_target + (u_probe - cal->u7) * cal->slope_high;
            }
        } else { // 2-point
            ph_val = 7.00f + (u_probe - cal->u7) * cal->slope_norm;
        }
    }

    // Bù nhiệt độ tuyến tính cho dung dịch (Linear Solution Temperature Compensation)
    // Ct = C25 * (1 + alpha * (T - 25)) => C25 = Ct / (1 + alpha * (T - 25))
    // ph_val thu được ở trên là pH thực tế tại nhiệt độ temp_c (Ct)
    // Ta quy đổi về pH chuẩn ở 25 độ C (C25) để hiển thị/ghi nhận
    float denom = 1.0f + g_temp_alpha * (temp_c - 25.0f);
    if (fabsf(denom) > 1e-4f) {
        ph_val = ph_val / denom;
    }

    // Kiểm tra tính hợp lệ của pH (phải từ 0.0 đến 14.0) trước khi clamp
    bool ph_is_valid = (ph_val >= 0.0f && ph_val <= 14.0f);
    if (out_ph_valid) {
        *out_ph_valid = ph_is_valid;
    }

    // Giới hạn dải đo bảo vệ hiển thị ngoài thực tế
    if (ph_val < 0.0f) {
        ph_val = 0.0f;
    }
    if (ph_val > 14.0f) {
        ph_val = 14.0f;
    }

    return ph_val;
}

// --- Các API Toàn cục mới phục vụ Web Server & Azure ---

static SemaphoreHandle_t s_sensor_status_mutex = NULL;

void ph_temp_init(void)
{
    if (s_sensor_status_mutex == NULL) {
        s_sensor_status_mutex = xSemaphoreCreateMutex();
    }
}

static void init_sensor_status_mutex_if_needed(void)
{
    ph_temp_init();
}

PH_Temp_Sensor_Status_t Get_Sensor_Status(void)
{
    init_sensor_status_mutex_if_needed();
    PH_Temp_Sensor_Status_t status;
    if (xSemaphoreTake(s_sensor_status_mutex, portMAX_DELAY) == pdTRUE) {
        status = s_sensor_status;
        xSemaphoreGive(s_sensor_status_mutex);
    } else {
        memset(&status, 0, sizeof(status));
    }
    return status;
}

void Update_Sensor_Measurements(float ph, bool ph_valid, float temp, bool temp_valid, float v_probe_mv)
{
    init_sensor_status_mutex_if_needed();
    if (xSemaphoreTake(s_sensor_status_mutex, portMAX_DELAY) == pdTRUE) {
        s_sensor_status.ph = ph;
        s_sensor_status.ph_valid = ph_valid;
        s_sensor_status.temperature = temp;
        s_sensor_status.temp_valid = temp_valid;
        s_sensor_status.v_probe_mv = v_probe_mv;

        // Đồng bộ các tham số hiệu chuẩn hiện tại
        s_sensor_status.is_calibrated = ph_cal.is_calibrated;
        s_sensor_status.ph7_voltage_mv = ph_cal.ph7_voltage_mv;
        s_sensor_status.ph7_temp_c = ph_cal.ph7_temp_c;
        s_sensor_status.ph4_voltage_mv = ph_cal.ph4_voltage_mv;
        s_sensor_status.ph4_temp_c = ph_cal.ph4_temp_c;
        s_sensor_status.slope_norm = ph_cal.slope_norm;
        s_sensor_status.u7 = ph_cal.u7;

        xSemaphoreGive(s_sensor_status_mutex);
    }
}

void Update_DO_Sensor_Measurements(float do_mg_l, float do_temp_c, float do_saturation_pct, bool do_valid, int do_error_code)
{
    init_sensor_status_mutex_if_needed();
    if (xSemaphoreTake(s_sensor_status_mutex, portMAX_DELAY) == pdTRUE) {
        s_sensor_status.do_mg_l = do_mg_l;
        s_sensor_status.do_temp_c = do_temp_c;
        s_sensor_status.do_saturation_pct = do_saturation_pct;
        s_sensor_status.do_valid = do_valid;
        s_sensor_status.do_error_code = do_error_code;
        xSemaphoreGive(s_sensor_status_mutex);
    }
}

bool Save_Calibration_To_Storage(const PhCalibration_t *cal)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("sys_cfg", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Lỗi mở NVS namespace sys_cfg để ghi: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_blob(handle, "ph_calib", cal, sizeof(PhCalibration_t));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Lỗi lưu/commit cấu hình hiệu chuẩn vào NVS: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "Đã lưu cấu hình hiệu chuẩn pH vào NVS thành công.");
    return true;
}

bool Load_Temp_Settings_From_Storage(void)
{
    uint32_t val = 0;
    if (Nvs_Read_Number("temp_mode", &val)) {
        g_temp_mode = (val < TEMP_MODE_COUNT) ? (temp_mode_t)val : TEMP_MODE_ATC_C;
        ESP_LOGI(TAG, "Load temp_mode from NVS: %d", g_temp_mode);
    } else {
        g_temp_mode = TEMP_MODE_ATC_C;
        ESP_LOGI(TAG, "No temp_mode in NVS, default ATC C");
    }

    if (Nvs_Read_Number("manual_temp", &val)) {
        g_manual_temp = (float)val / 10.0f;
        ESP_LOGI(TAG, "Load manual_temp from NVS: %.1f", g_manual_temp);
    } else {
        g_manual_temp = 25.0f;
        ESP_LOGI(TAG, "No manual_temp in NVS, default 25.0");
    }

    if (Nvs_Read_Number("temp_alpha", &val)) {
        g_temp_alpha = (float)((int32_t)val) / 1000.0f;
        ESP_LOGI(TAG, "Load temp_alpha from NVS: %.3f", g_temp_alpha);
    } else {
        g_temp_alpha = 0.0f;
        ESP_LOGI(TAG, "No temp_alpha in NVS, default 0.000");
    }

    if (Nvs_Read_Number("temp_offset", &val)) {
        g_temp_offset = (float)((int32_t)val) / 10.0f;
        ESP_LOGI(TAG, "Load temp_offset from NVS: %.1f", g_temp_offset);
    } else {
        g_temp_offset = 0.0f;
        ESP_LOGI(TAG, "No temp_offset in NVS, default 0.0");
    }
    return true;
}

bool Save_Temp_Settings_To_Storage(void)
{
    bool ok = true;
    ok &= Nvs_Write_Number("temp_mode", (uint32_t)g_temp_mode);
    ok &= Nvs_Write_Number("manual_temp", (uint32_t)(g_manual_temp * 10.0f));
    ok &= Nvs_Write_Number("temp_alpha", (uint32_t)(int32_t)(g_temp_alpha * 1000.0f));
    ok &= Nvs_Write_Number("temp_offset", (uint32_t)(int32_t)(g_temp_offset * 10.0f));
    if (ok) {
        ESP_LOGI(TAG, "Saved temp settings to NVS: mode=%d, manual=%.1f, alpha=%.3f, offset=%.1f",
                 g_temp_mode, g_manual_temp, g_temp_alpha, g_temp_offset);
    } else {
        ESP_LOGE(TAG, "Failed to save temp settings to NVS!");
    }
    return ok;
}

bool Load_Calibration_From_Storage(void)
{
    ph_temp_init();
    Load_Temp_Settings_From_Storage();
    nvs_handle_t handle;
    esp_err_t err = nvs_open("sys_cfg", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Không mở được NVS (Lần đầu boot?): %s. Khởi tạo giá trị giả lập thử nghiệm.", esp_err_to_name(err));
        // --- Giá trị cũ gốc: ph7_mV=24.2f, ph7_temp=25.0f, ph4_mV=195.6f, ph4_temp=25.0f, is_calibrated=false ---
        ph_cal.ph7_voltage_mv = 1.63f;  // Gốc: 24.2f
        ph_cal.ph7_temp_c = 30.97f;    // Gốc: 25.0f
        ph_cal.ph4_voltage_mv = 176.80f; // Gốc: 195.6f
        ph_cal.ph4_temp_c = 30.89f;    // Gốc: 25.0f
        ph_cal.ph10_voltage_mv = -177.0f;
        ph_cal.ph10_temp_c = 25.0f;
        ph_cal.ph10_target = 10.00f;
        ph_cal.cal_type = 2;
        update_ph_calibration(&ph_cal);
        Update_Sensor_Measurements(7.0f, true, 25.0f, true, 0.0f);
        return false;
    }

    size_t size = sizeof(PhCalibration_t);
    err = nvs_get_blob(handle, "ph_calib", &ph_cal, &size);
    nvs_close(handle);

    if (err != ESP_OK || size != sizeof(PhCalibration_t)) {
        ESP_LOGW(TAG, "Không tìm thấy dữ liệu hiệu chuẩn trong NVS, sử dụng giá trị fake thử nghiệm.");
        // --- Giá trị cũ gốc: ph7_mV=24.2f, ph7_temp=25.0f, ph4_mV=195.6f, ph4_temp=25.0f, is_calibrated=false ---
        ph_cal.ph7_voltage_mv = 1.63f;  // Gốc: 24.2f
        ph_cal.ph7_temp_c = 30.97f;    // Gốc: 25.0f
        ph_cal.ph4_voltage_mv = 176.80f; // Gốc: 195.6f
        ph_cal.ph4_temp_c = 30.89f;    // Gốc: 25.0f
        ph_cal.ph10_voltage_mv = -177.0f;
        ph_cal.ph10_temp_c = 25.0f;
        ph_cal.ph10_target = 10.00f;
        ph_cal.cal_type = 2;
        update_ph_calibration(&ph_cal);
        Update_Sensor_Measurements(7.0f, true, 25.0f, true, 0.0f);
        return false;
    }

    // Ghi đè bộ thông số fake thử nghiệm để đảm bảo luôn dùng thông số này khi giả lập
    ph_cal.ph7_voltage_mv = 1.63f;
    ph_cal.ph7_temp_c = 30.97f;
    ph_cal.ph4_voltage_mv = 176.80f;
    ph_cal.ph4_temp_c = 30.89f;
    ph_cal.cal_type = 2;
    update_ph_calibration(&ph_cal);

    ESP_LOGI(TAG, "Đã nạp cấu hình hiệu chuẩn thử nghiệm (fake): is_calibrated=%d, pH7_mV=%.2f (%.2fC), pH4_mV=%.2f (%.2fC)",
             ph_cal.is_calibrated, ph_cal.ph7_voltage_mv, ph_cal.ph7_temp_c, ph_cal.ph4_voltage_mv, ph_cal.ph4_temp_c);
    Update_Sensor_Measurements(7.0f, true, 25.0f, true, 0.0f);
    return true;
}

bool Calibrate_PH_Point(float target_ph, float current_v_mv, float current_temp_c, uint8_t cal_type)
{
    ESP_LOGI(TAG, "Yêu cầu hiệu chuẩn pH %.2f: raw mV=%.2f, Temp=%.2f C, cal_type=%d",
             target_ph, current_v_mv, current_temp_c, cal_type);

    PhCalibration_t temp_cal = ph_cal;
    temp_cal.cal_type = cal_type;

    if (fabsf(target_ph - 4.00f) < 0.1f) {
        if (current_v_mv < 120.0f || current_v_mv > 240.0f) {
            ESP_LOGE(TAG, "Hiệu chuẩn pH 4.00 thất bại: Điện áp %.2f mV ngoài dải hợp lệ [+120mV, +240mV]!", current_v_mv);
            return false;
        }
        temp_cal.ph4_voltage_mv = current_v_mv;
        temp_cal.ph4_temp_c = current_temp_c;
    } else if (fabsf(target_ph - 7.00f) < 0.1f || fabsf(target_ph - 6.86f) < 0.1f) {
        if (current_v_mv < -60.0f || current_v_mv > 60.0f) {
            ESP_LOGE(TAG, "Hiệu chuẩn pH %.2f thất bại: Điện áp %.2f mV ngoài dải hợp lệ [-60mV, +60mV]!", target_ph, current_v_mv);
            return false;
        }
        temp_cal.ph7_voltage_mv = current_v_mv;
        temp_cal.ph7_temp_c = current_temp_c;
    } else if (fabsf(target_ph - 9.18f) < 0.1f || fabsf(target_ph - 10.00f) < 0.1f) {
        if (current_v_mv < -240.0f || current_v_mv > -100.0f) {
            ESP_LOGE(TAG, "Hiệu chuẩn pH %.2f thất bại: Điện áp %.2f mV ngoài dải hợp lệ [-240mV, -100mV]!", target_ph, current_v_mv);
            return false;
        }
        temp_cal.ph10_voltage_mv = current_v_mv;
        temp_cal.ph10_temp_c = current_temp_c;
        temp_cal.ph10_target = target_ph;
    } else {
        ESP_LOGE(TAG, "Hiệu chuẩn thất bại: Mốc pH %.2f không hỗ trợ!", target_ph);
        return false;
    }

    update_ph_calibration(&temp_cal);

    if (!temp_cal.is_calibrated) {
        ESP_LOGE(TAG, "Hiệu chuẩn pH thất bại: Độ dốc Slope không đạt yêu cầu sức khỏe điện cực (80%% - 110%%)!");
        return false;
    }

    ph_cal = temp_cal;
    Save_Calibration_To_Storage(&ph_cal);
    Update_Sensor_Measurements(s_sensor_status.ph, s_sensor_status.ph_valid, s_sensor_status.temperature, s_sensor_status.temp_valid, s_sensor_status.v_probe_mv);
    return true;
}

bool Reset_PH_Calibration(void)
{
    ESP_LOGI(TAG, "Yêu cầu khôi phục cài đặt gốc hiệu chuẩn pH...");

    // --- Giá trị cũ gốc: ph7_mV=4.77f, ph7_temp=25.0f, ph4_mV=175.53f, ph4_temp=25.0f, is_calibrated=false ---
    ph_cal.ph7_voltage_mv = 1.63f;  // Gốc: 4.77f
    ph_cal.ph7_temp_c = 30.97f;    // Gốc: 25.0f
    ph_cal.ph4_voltage_mv = 176.80f; // Gốc: 175.53f
    ph_cal.ph4_temp_c = 30.89f;    // Gốc: 25.0f
    ph_cal.ph10_voltage_mv = -177.0f;
    ph_cal.ph10_temp_c = 25.0f;
    ph_cal.ph10_target = 10.00f;
    ph_cal.cal_type = 2;

    update_ph_calibration(&ph_cal);

    bool ok = Save_Calibration_To_Storage(&ph_cal);
    Update_Sensor_Measurements(s_sensor_status.ph, s_sensor_status.ph_valid, s_sensor_status.temperature, s_sensor_status.temp_valid, s_sensor_status.v_probe_mv);
    return ok;
}

PhSensorHealth_t Get_PH_Sensor_Health(void)
{
    PhSensorHealth_t health;
    memset(&health, 0, sizeof(health));

    health.is_calibrated = ph_cal.is_calibrated;
    health.zero_offset_mv = ph_cal.ph7_voltage_mv;

    float t7_k = ph_cal.ph7_temp_c + 273.15f;
    float t4_k = ph_cal.ph4_temp_c + 273.15f;
    float u7 = ph_cal.ph7_voltage_mv / t7_k;
    float u4 = ph_cal.ph4_voltage_mv / t4_k;
    float du_low = fabsf(u7 - u4);

    health.sens_mv_per_ph = (du_low * 298.15f) / 3.00f; // mV/pH ở 25°C
    health.slope_pct = (health.sens_mv_per_ph / 59.16f) * 100.0f;

    // Điện cực được coi là khỏe (Healthy) khi:
    // 1. Hệ thống đã hiệu chuẩn thành công
    // 2. Độ nhạy Slope >= 80.0%
    // 3. Điểm lệch 0 (Zero Offset) trong khoảng [-30.0mV, +30.0mV]
    if (health.is_calibrated && health.slope_pct >= 80.0f && fabsf(health.zero_offset_mv) <= 30.0f) {
        health.is_healthy = true;
    } else {
        health.is_healthy = false;
    }

    return health;
}
