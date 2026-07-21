#include "filter.h"
#include <stddef.h>
#include <stdlib.h>
#include <math.h>

filter_level_t g_filter_level = FILTER_LEVEL_L;

MedianFilter_t ph_median_filter = {0};
MedianFilter_t temp_median_filter = {0};

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

void init_median_filter(MedianFilter_t *filter, int32_t initial_val) {
    if (filter->mutex == NULL) {
        filter->mutex = xSemaphoreCreateMutex();
    }

    if (filter->mutex != NULL) {
        xSemaphoreTake(filter->mutex, portMAX_DELAY);
    }

    filter->index = 0;
    filter->count = MEDIAN_FILTER_SIZE;
    for (int i = 0; i < MEDIAN_FILTER_SIZE; i++) {
        filter->buffer[i] = initial_val;
    }

    if (filter->mutex != NULL) {
        xSemaphoreGive(filter->mutex);
    }
}

int32_t apply_median_filter(MedianFilter_t *filter, int32_t new_val) {
    int32_t median = new_val;

    if (filter->mutex != NULL) {
        xSemaphoreTake(filter->mutex, portMAX_DELAY);
    }

    filter->buffer[filter->index] = new_val;
    filter->index = (filter->index + 1) % MEDIAN_FILTER_SIZE;
    if (filter->count < MEDIAN_FILTER_SIZE) {
        filter->count++;
    }

    int32_t temp[MEDIAN_FILTER_SIZE];
    int n = filter->count;
    for (int i = 0; i < n; i++) {
        temp[i] = filter->buffer[i];
    }

    // Sort using insertion sort
    for (int i = 1; i < n; i++) {
        int32_t key = temp[i];
        int j = i - 1;
        while (j >= 0 && temp[j] > key) {
            temp[j + 1] = temp[j];
            j = j - 1;
        }
        temp[j + 1] = key;
    }

    median = temp[n / 2];

    if (filter->mutex != NULL) {
        xSemaphoreGive(filter->mutex);
    }

    return median;
}

SpikeFilter_t ph_spike_filter = {0};
SpikeFilter_t temp_spike_filter = {0};

void reset_moving_average_val(MovingAverage_t *filter, int32_t fill_val) {
    if (filter->mutex != NULL) {
        xSemaphoreTake(filter->mutex, portMAX_DELAY);
    }

    uint16_t current_size = filter->size;
    if (current_size == 0 || current_size > MAX_FILTER_SIZE) {
        current_size = FILTER_L_SIZE;
    }

    for (int i = 0; i < MAX_FILTER_SIZE; i++) {
        filter->buffer[i] = fill_val;
    }
    filter->index = 0;
    filter->is_filled = true;
    filter->running_sum = (int64_t)fill_val * current_size;

    if (filter->mutex != NULL) {
        xSemaphoreGive(filter->mutex);
    }
}

void reset_median_filter_val(MedianFilter_t *filter, int32_t fill_val) {
    if (filter->mutex != NULL) {
        xSemaphoreTake(filter->mutex, portMAX_DELAY);
    }

    filter->index = 0;
    filter->count = MEDIAN_FILTER_SIZE;
    for (int i = 0; i < MEDIAN_FILTER_SIZE; i++) {
        filter->buffer[i] = fill_val;
    }

    if (filter->mutex != NULL) {
        xSemaphoreGive(filter->mutex);
    }
}

void init_spike_filter(SpikeFilter_t *filter, int32_t initial_val, int32_t max_delta, uint8_t max_allowed_spikes) {
    if (filter->mutex == NULL) {
        filter->mutex = xSemaphoreCreateMutex();
    }
    if (filter->mutex != NULL) {
        xSemaphoreTake(filter->mutex, portMAX_DELAY);
    }

    filter->last_valid_val = initial_val;
    filter->max_delta = max_delta;
    filter->consecutive_spikes = 0;
    filter->max_allowed_spikes = max_allowed_spikes;
    filter->is_initialized = true;

    if (filter->mutex != NULL) {
        xSemaphoreGive(filter->mutex);
    }
}

int32_t apply_spike_filter(SpikeFilter_t *filter, int32_t new_val, bool *out_step_detected) {
    int32_t result = new_val;
    bool step_detected = false;

    if (filter->mutex != NULL) {
        xSemaphoreTake(filter->mutex, portMAX_DELAY);
    }

    if (!filter->is_initialized) {
        filter->last_valid_val = new_val;
        filter->consecutive_spikes = 0;
        filter->is_initialized = true;
    }

    int32_t delta = labs(new_val - filter->last_valid_val);
    if (delta > filter->max_delta) {
        filter->consecutive_spikes++;
        if (filter->consecutive_spikes <= filter->max_allowed_spikes) {
            // Từ chối giá trị nhiễu đột biến, trả về giá trị hợp lệ gần nhất
            result = filter->last_valid_val;
            step_detected = false;
        } else {
            // Nhiễu kéo dài quá max_allowed_spikes -> Xác nhận là bước nhảy thực sự!
            filter->last_valid_val = new_val;
            filter->consecutive_spikes = 0;
            result = new_val;
            step_detected = true;
        }
    } else {
        // Tín hiệu biến thiên trong ngưỡng hợp lệ
        filter->last_valid_val = new_val;
        filter->consecutive_spikes = 0;
        result = new_val;
        step_detected = false;
    }

    if (filter->mutex != NULL) {
        xSemaphoreGive(filter->mutex);
    }

    if (out_step_detected) {
        *out_step_detected = step_detected;
    }

    return result;
}

EwmaFilter_t temp_ewma_filter = {0};
Kalman1D_t ph_kalman_filter = {0};
Kalman1D_t do_kalman_filter = {0};

void ewma_init(EwmaFilter_t *filter, float initial_val, float alpha) {
    if (filter->mutex == NULL) {
        filter->mutex = xSemaphoreCreateMutex();
    }
    if (filter->mutex != NULL) {
        xSemaphoreTake(filter->mutex, portMAX_DELAY);
    }
    filter->last_val = initial_val;
    filter->alpha = alpha;
    filter->is_initialized = true;
    if (filter->mutex != NULL) {
        xSemaphoreGive(filter->mutex);
    }
}

float ewma_update(EwmaFilter_t *filter, float input) {
    float output = input;
    if (filter->mutex != NULL) {
        xSemaphoreTake(filter->mutex, portMAX_DELAY);
    }
    if (!filter->is_initialized) {
        filter->last_val = input;
        filter->is_initialized = true;
    }
    output = filter->alpha * input + (1.0f - filter->alpha) * filter->last_val;
    filter->last_val = output;

    if (filter->mutex != NULL) {
        xSemaphoreGive(filter->mutex);
    }
    return output;
}

void ewma_reset(EwmaFilter_t *filter, float reset_val) {
    if (filter->mutex != NULL) {
        xSemaphoreTake(filter->mutex, portMAX_DELAY);
    }
    filter->last_val = reset_val;
    filter->is_initialized = true;
    if (filter->mutex != NULL) {
        xSemaphoreGive(filter->mutex);
    }
}

void kalman1d_init(Kalman1D_t *k, float initial_val, float Q, float R) {
    if (k->mutex == NULL) {
        k->mutex = xSemaphoreCreateMutex();
    }
    if (k->mutex != NULL) {
        xSemaphoreTake(k->mutex, portMAX_DELAY);
    }
    k->x = initial_val;
    k->P = 1.0f;
    k->Q = Q;
    k->R = R;
    k->is_initialized = true;
    if (k->mutex != NULL) {
        xSemaphoreGive(k->mutex);
    }
}

float kalman1d_update(Kalman1D_t *k, float measurement) {
    float result = measurement;
    if (k->mutex != NULL) {
        xSemaphoreTake(k->mutex, portMAX_DELAY);
    }
    if (!k->is_initialized) {
        k->x = measurement;
        k->P = 1.0f;
        k->is_initialized = true;
    }
    k->P = k->P + k->Q;
    float K = k->P / (k->P + k->R);
    k->x = k->x + K * (measurement - k->x);
    k->P = (1.0f - K) * k->P;
    result = k->x;

    if (k->mutex != NULL) {
        xSemaphoreGive(k->mutex);
    }
    return result;
}

void kalman1d_reset(Kalman1D_t *k, float reset_val) {
    if (k->mutex != NULL) {
        xSemaphoreTake(k->mutex, portMAX_DELAY);
    }
    k->x = reset_val;
    k->P = 1.0f;
    k->is_initialized = true;
    if (k->mutex != NULL) {
        xSemaphoreGive(k->mutex);
    }
}
