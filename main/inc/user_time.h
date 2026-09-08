#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "driver/gptimer.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

void User_Time_Task(void);

#ifdef __cplusplus
}
#endif



