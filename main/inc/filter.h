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

extern MovingAverage_t ph_filter;
extern MovingAverage_t temp_filter;

void init_moving_average(MovingAverage_t *filter);
int32_t apply_moving_average(MovingAverage_t *filter, int32_t new_val);
void set_moving_average_size(MovingAverage_t *filter, uint16_t new_size);
void update_system_filters_level(filter_level_t level);

#endif // FILTER_H
