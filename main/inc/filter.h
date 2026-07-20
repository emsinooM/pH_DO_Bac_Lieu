#ifndef FILTER_H
#define FILTER_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define FILTER_L_SIZE 10    // 5s = 10 samples (at 500ms sampling)
#define FILTER_M_SIZE 20    // 10s = 20 samples
#define FILTER_H_SIZE 40    // 20s = 40 samples
#define MAX_FILTER_SIZE FILTER_H_SIZE

#define MEDIAN_FILTER_SIZE 5

typedef enum {
    FILTER_LEVEL_L = 0,     // Low (5s)
    FILTER_LEVEL_M,         // Middle (10s)
    FILTER_LEVEL_H,         // High (20s)
    FILTER_LEVEL_COUNT
} filter_level_t;

extern filter_level_t g_filter_level;

typedef struct {
    int32_t buffer[MAX_FILTER_SIZE];
    uint16_t index;
    bool is_filled;
    int64_t running_sum;     // Chạy tổng được duy trì để đạt độ phức tạp O(1)
    SemaphoreHandle_t mutex; // Thêm Mutex bảo vệ tránh race condition
    uint16_t size;           // Kích thước lọc thực tế
} MovingAverage_t;

typedef struct {
    int32_t buffer[MEDIAN_FILTER_SIZE];
    uint8_t index;
    uint8_t count;
    SemaphoreHandle_t mutex;
} MedianFilter_t;

#define SPIKE_PH_MAX_DELTA_RAW 31000   // Tương đương ~0.3 pH
#define SPIKE_TEMP_MAX_DELTA_RAW 50000 // Tương đương ~1.0 °C
#define SPIKE_MAX_ALLOWED_COUNT 4      // 4 mẫu = 2.0 giây

typedef struct {
    int32_t last_valid_val;
    int32_t max_delta;
    uint8_t consecutive_spikes;
    uint8_t max_allowed_spikes;
    bool is_initialized;
    SemaphoreHandle_t mutex;
} SpikeFilter_t;

extern MovingAverage_t ph_filter;
extern MovingAverage_t temp_filter;
extern MedianFilter_t ph_median_filter;
extern MedianFilter_t temp_median_filter;
extern SpikeFilter_t ph_spike_filter;
extern SpikeFilter_t temp_spike_filter;

void init_moving_average(MovingAverage_t *filter);
int32_t apply_moving_average(MovingAverage_t *filter, int32_t new_val);
void set_moving_average_size(MovingAverage_t *filter, uint16_t new_size);
void update_system_filters_level(filter_level_t level);

void init_median_filter(MedianFilter_t *filter, int32_t initial_val);
int32_t apply_median_filter(MedianFilter_t *filter, int32_t new_val);

void init_spike_filter(SpikeFilter_t *filter, int32_t initial_val, int32_t max_delta, uint8_t max_allowed_spikes);
int32_t apply_spike_filter(SpikeFilter_t *filter, int32_t new_val, bool *out_step_detected);

void reset_moving_average_val(MovingAverage_t *filter, int32_t fill_val);
void reset_median_filter_val(MedianFilter_t *filter, int32_t fill_val);

#endif // FILTER_H
