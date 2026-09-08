#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Hằng số dùng chung cho tính toán pH & Nhiệt độ
// Định nghĩa lại 2 mốc điện áp riêng biệt
#define V_REF_ADC       2.048f  // <-- THAY BẰNG ÁP VDD BẠN ĐO ĐƯỢC TẠI CHÂN 7
#define V_REF_BRIDGE    2.048f
#define PGA             1.0f
#define ADC_SCALE       8388607.0f
#define ADC_DIVISOR     (ADC_SCALE * 2.0f * PGA)

#define NTC_T0_KELVIN               (25.0f + 273.15f)
#define NTC_NOMINAL_RESISTANCE      10000.0f
#define NTC_BETA                    3950.0f

#define R_CALIB 750.0f

// --- Cấu trúc dữ liệu Hiệu chuẩn pH 2 điểm và 3 điểm nâng cao ---
typedef struct {
    float ph7_voltage_mv;   // Hiệu điện thế thực tế đo được tại pH 7.00 hoặc 6.86 (mV)
    float ph7_temp_c;       // Nhiệt độ dung dịch đo lúc hiệu chuẩn pH 7/6.86 (C)
    float ph4_voltage_mv;   // Hiệu điện thế thực tế đo được tại pH 4.00 (mV)
    float ph4_temp_c;       // Nhiệt độ dung dịch đo lúc hiệu chuẩn pH 4 (C)
    float ph10_voltage_mv;  // Hiệu điện thế thực tế đo được tại pH 9.18 hoặc 10.00 (mV)
    float ph10_temp_c;      // Nhiệt độ dung dịch đo lúc hiệu chuẩn pH 9.18/10.00 (C)
    float ph10_target;      // Giá trị pH mục tiêu kiềm (9.18 hoặc 10.00)

    float slope_norm;       // Độ dốc chuẩn hóa axit (pH * K / mV) (cho pH <= 7.00)
    float slope_high;       // Độ dốc chuẩn hóa kiềm (pH * K / mV) (cho pH > 7.00)
    float u7;               // Điểm lệch chuẩn hóa thực tế tại pH 7.00/6.86 (mV/K)

    uint8_t cal_type;       // 2 = Hiệu chuẩn 2 điểm, 3 = Hiệu chuẩn 3 điểm
    bool is_calibrated;     // Trạng thái xác định hệ thống đã được hiệu chuẩn thành công hay chưa
} PhCalibration_t;

extern PhCalibration_t ph_cal;

// --- Cấu trúc cấu hình Nhiệt độ ---
typedef enum {
    TEMP_MODE_ATC_C = 0, // Bù nhiệt tự động, đơn vị °C
    TEMP_MODE_MTC_C,     // Bù nhiệt thủ công, đơn vị °C
    TEMP_MODE_ATC_F,     // Bù nhiệt tự động, đơn vị °F
    TEMP_MODE_MTC_F,     // Bù nhiệt thủ công, đơn vị °F
    TEMP_MODE_COUNT
} temp_mode_t;

extern temp_mode_t g_temp_mode;
extern float g_manual_temp;
extern float g_temp_alpha;
extern float g_temp_offset;

bool Load_Temp_Settings_From_Storage(void);
bool Save_Temp_Settings_To_Storage(void);

// --- Cấu trúc trạng thái toàn cục của Cảm biến ---
typedef struct {
    float ph;               // Giá trị pH hiện tại
    float temperature;      // Giá trị nhiệt độ hiện tại (C)
    float v_probe_mv;       // Điện áp đo được từ đầu dò (mV)
    bool is_calibrated;     // Đã được hiệu chuẩn hay chưa
    bool ph_valid;          // Trạng thái dữ liệu pH hợp lệ (0 <= pH <= 14)
    bool temp_valid;        // Trạng thái dữ liệu nhiệt độ hợp lệ (0 <= Temp <= 60 C cho ATC)

    // Các tham số hiệu chuẩn hiện tại
    float ph7_voltage_mv;
    float ph7_temp_c;
    float ph4_voltage_mv;
    float ph4_temp_c;
    float slope_norm;
    float u7;

    // Cảm biến DO (Dissolved Oxygen)
    float do_mg_l;          // Nồng độ DO (mg/L)
    float do_temp_c;        // Nhiệt độ cảm biến DO (C)
    float do_saturation_pct;// Độ bão hòa DO (%)
    bool do_valid;          // Trạng thái dữ liệu hợp lệ
    int do_error_code;      // Mã lỗi nếu do_valid = false
} PH_Temp_Sensor_Status_t;

// Hàm cập nhật tham số hiệu chuẩn từ phép đo thực tế (Gọi khi nhúng dung dịch chuẩn thành công)
void update_ph_calibration(PhCalibration_t *cal);
// Tính toán nhiệt độ từ ADC thô (PT1000 hoặc NTC)
float calculate_temperature(int32_t raw_adc, bool is_pt1000);

// Tính toán pH bù nhiệt (ATC) kết hợp dữ liệu hiệu chuẩn thực tế 2 điểm
float calculate_ph_with_atc_calibrated(PhCalibration_t *cal, int32_t raw_adc, float temp_c, float *out_v_probe_mv, bool *out_ph_valid);

// --- Các API Toàn cục mới phục vụ Web Server & Azure ---
void ph_temp_init(void);
PH_Temp_Sensor_Status_t Get_Sensor_Status(void);
void Update_Sensor_Measurements(float ph, bool ph_valid, float temp, bool temp_valid, float v_probe_mv);
void Update_DO_Sensor_Measurements(float do_mg_l, float do_temp_c, float do_saturation_pct, bool do_valid, int do_error_code);
bool Load_Calibration_From_Storage(void);
bool Save_Calibration_To_Storage(const PhCalibration_t *cal);
bool Calibrate_PH_Point(float target_ph, float current_v_mv, float current_temp_c, uint8_t cal_type);
bool Reset_PH_Calibration(void);

// --- Cấu trúc chẩn đoán Sức khỏe Cảm biến pH ---
typedef struct {
    float slope_pct;       // Độ nhạy chuẩn hóa % so với Nernst (ví dụ 97.5%)
    float zero_offset_mv;  // Điểm lệch 0 tại pH 7.00 (mV) (ví dụ -12.0 mV)
    float sens_mv_per_ph;  // Độ dốc tính bằng mV/pH ở 25C (ví dụ 57.68 mV/pH)
    bool  is_calibrated;   // Trạng thái đã hiệu chuẩn thực tế hay chưa
    bool  is_healthy;      // true nếu Slope >= 80% và |Zero| <= 30mV
} PhSensorHealth_t;

PhSensorHealth_t Get_PH_Sensor_Health(void);

#ifdef __cplusplus
}
#endif
