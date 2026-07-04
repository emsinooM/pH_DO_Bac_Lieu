#pragma once

#include "esp_https_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#define OTA_WAIT_BIT   (1<<0)




extern EventGroupHandle_t otaEventGroup;
extern char g_ota_update_url[512];

typedef enum {
    OTA_STATUS_IDLE = 0,
    OTA_STATUS_WAIT_AZURE,
    OTA_STATUS_DOWNLOADING,
    OTA_STATUS_SUCCESS,
    OTA_STATUS_FAILED
} user_ota_status_t;

extern user_ota_status_t g_ota_status;
extern char g_ota_err_desc[64];

const char* User_Ota_Get_Status_String(void);

void User_Ota_Task(void *pvParameters);
esp_err_t update_firmware(const char *updateFileName);
void User_Ota_Trigger(const char *url);


