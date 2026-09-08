#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"

#include "user_azure.h"
#include "user_storage.h"
#include "user_system.h"
#include "wifi_config_manager.h"

Sys_Info_Handle_t Sys_Info;
uint32_t reset_count = 0;

static void User_System_Update_Reset_Count(void)
{
    uint32_t count = 0;
    if (!Nvs_Read_Number(STORAGE_KEY_RESET_COUNT, &count)) {
        count = 0;
    }

    esp_reset_reason_t reason = esp_reset_reason();
    if (!(reason == ESP_RST_POWERON && count == 0)) {
        count++;
        Nvs_Write_Number(STORAGE_KEY_RESET_COUNT, count);
    }

    reset_count = count;
    ESP_LOGI("SYS INIT", "Reset reason=%d, reset_count=%u", (int)reason, (unsigned)reset_count);
}

void User_System_Clear_Reset_Count(void)
{
    reset_count = 0;
    Nvs_Write_Number(STORAGE_KEY_RESET_COUNT, 0);
    ESP_LOGI("SYS INIT", "Reset count cleared");
}



void User_System_Get_Config(void)
{
    memset(&IoTHubHandle, 0, sizeof(IoTHubHandle));

    ESP_LOGI("SYS INIT", "Get Tcp parameters");

    char az_host[64] = {0};
    char az_dev[64] = {0};
    char az_sym[64] = {0};

    if (azure_config_manager_load(az_host, sizeof(az_host), az_dev, sizeof(az_dev), az_sym, sizeof(az_sym))) {
        ESP_LOGI("SYS INIT", "Loaded Azure Config from FRAM: Host=%s, Device=%s", az_host, az_dev);
        strncpy(IoTHubHandle.hostName, az_host, sizeof(IoTHubHandle.hostName) - 1);
        strncpy(IoTHubHandle.deviceId, az_dev, sizeof(IoTHubHandle.deviceId) - 1);
        strncpy(IoTHubHandle.symmetricKey, az_sym, sizeof(IoTHubHandle.symmetricKey) - 1);
    } else {
        ESP_LOGW("SYS INIT", "Azure Config FRAM empty/invalid, waiting for Web Config...");
        memset(IoTHubHandle.hostName, 0, sizeof(IoTHubHandle.hostName));
        memset(IoTHubHandle.deviceId, 0, sizeof(IoTHubHandle.deviceId));
        memset(IoTHubHandle.symmetricKey, 0, sizeof(IoTHubHandle.symmetricKey));
    }
}

bool Is_System_Time_Synchronized(void)
{
    return Sys_Info.isTimeSync;
}

bool Is_System_Internet_Connected(void)
{
    return Sys_Info.isWifiConnected;
}



void User_System_Init(void)
{
    /* Get config parameters */
    ESP_LOGI("SYS INIT", "");

    User_System_Update_Reset_Count();
    User_System_Get_Config();
}
