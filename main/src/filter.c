#include "filter.h"
#include <stddef.h>
#include <math.h>

filter_level_t g_filter_level = FILTER_LEVEL_L;

// Hàm khởi tạo bộ lọc và Mutex
void init_moving_average(MovingAverage_t *filter) {
    filter->size = FILTER_L_SIZE; // Mặc định là 5s (10 mẫu)
    for (int i = 0; i < MAX_FILTER_SIZE; i++) {
        filter->buffer[i] = 0;
    }
    filter->index = 0;
    filter->is_filled = false;
    filter->running_sum = 0;
    filter->mutex = xSemaphoreCreateMutex();
}

int32_t apply_moving_average(MovingAverage_t *filter, int32_t new_val) {
    int32_t avg = 0;

    // Khóa Mutex trước khi can thiệp vào buffer
    if (filter->mutex != NULL) {
        xSemaphoreTake(filter->mutex, portMAX_DELAY);
    }

    uint16_t current_size = filter->size;
    if (current_size == 0) {
        current_size = FILTER_L_SIZE;
    }
    if (current_size > MAX_FILTER_SIZE) {
        current_size = MAX_FILTER_SIZE;
    }

    if (filter->is_filled) {
        filter->running_sum -= filter->buffer[filter->index];
    }
    filter->running_sum += new_val;
    filter->buffer[filter->index] = new_val;
    
    filter->index++;
    if (filter->index >= current_size) {
        filter->index = 0;
        filter->is_filled = true;
    }

    int count = filter->is_filled ? current_size : filter->index;
    if (count == 0) {
        count = 1;
    }
    avg = (int32_t)(filter->running_sum / count);

    // Mở khóa Mutex sau khi hoàn tất
    if (filter->mutex != NULL) {
        xSemaphoreGive(filter->mutex);
    }

    return avg;
}

void set_moving_average_size(MovingAverage_t *filter, uint16_t new_size) {
    if (filter->mutex != NULL) {
        xSemaphoreTake(filter->mutex, portMAX_DELAY);
    }

    if (new_size > MAX_FILTER_SIZE) {
        new_size = MAX_FILTER_SIZE;
    }
    if (new_size == 0) {
        new_size = 1;
    }

    filter->size = new_size;
    for (int i = 0; i < MAX_FILTER_SIZE; i++) {
        filter->buffer[i] = 0;
    }
    filter->index = 0;
    filter->is_filled = false;
    filter->running_sum = 0;

    if (filter->mutex != NULL) {
        xSemaphoreGive(filter->mutex);
    }
}

void update_system_filters_level(filter_level_t level) {
    uint16_t size = FILTER_L_SIZE;
    if (level == FILTER_LEVEL_M) {
        size = FILTER_M_SIZE;
    } else if (level == FILTER_LEVEL_H) {
        size = FILTER_H_SIZE;
    }

    set_moving_average_size(&ph_filter, size);
    set_moving_average_size(&temp_filter, size);
}
