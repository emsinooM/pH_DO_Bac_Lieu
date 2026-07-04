#include "user_system.h"
#include "tcp_server_com.h"
#include "user_storage.h"
#include "esp_log.h"
#include "user_azure.h"
#include "esp_system.h"
#include "wifi_config_manager.h"

Sys_Info_Handle_t Sys_Info;
uint32_t reset_count = 0;

static void User_System_Update_Reset_Count(void)
{
    uint32_t count = 0;
    if(!Nvs_Read_Number(STORAGE_KEY_RESET_COUNT, &count))
    {
        count = 0;
    }

    esp_reset_reason_t reason = esp_reset_reason();
    if(!(reason == ESP_RST_POWERON && count == 0))
    {
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
    memset(TCP_Handle.remoteIP, 0, sizeof(TCP_Handle.remoteIP));
    memset(TCP_Handle.ssid, 0, sizeof(TCP_Handle.ssid));
    memset(TCP_Handle.pass, 0, sizeof(TCP_Handle.pass));

    memset(&IoTHubHandle, 0, sizeof(IoTHubHandle));

    ESP_LOGI("SYS INIT", "Get Tcp parameters");
    // if(Nvs_Read_String(STORAGE_KEY_SEVER_IP, TCP_Handle.remoteIP) == true)
    // {
    //     ESP_LOGI("SYS INIT", "SERVER_IP: %s", TCP_Handle.remoteIP);
    // }else{
    //     ESP_LOGE("SYS INIT", "Read SERVER_IP fail, set default: %s", SYS_SERVER_IP_DEFAULT);
    // }

    // if(Nvs_Read_Number(STORAGE_KEY_SEVER_PORT, &TCP_Handle.remotePort) == true)
    // {
    //     ESP_LOGI("SYS INIT", "SERVER_PORT: %d", TCP_Handle.remotePort);
    // }else{
    //     ESP_LOGE("SYS INIT", "Read SERVER_PORT fail, set default: %d", SYS_SERVER_PORT_DEFAULT);
    // }
    
    // if(Nvs_Read_String(STORAGE_KEY_WIFI_SSID, TCP_Handle.ssid) == true)
    // {
    //     ESP_LOGI("SYS INIT", "WIFI_SSID: %s", TCP_Handle.ssid);
    // }else{
    //     ESP_LOGE("SYS INIT", "Read WIFI_SSID fail, set default: %s", SYS_WIFI_SSID_DEFAULT);
    // }

    // if(Nvs_Read_String(STORAGE_KEY_WIFI_PASS, TCP_Handle.pass) == true)
    // {
    //     ESP_LOGI("SYS INIT", "WIFI_PASS: %s", TCP_Handle.pass);
    // }else{
    //     ESP_LOGE("SYS INIT", "Read WIFI_PASS fail, set default: %s", SYS_WIFI_PASS_DEFAULT);
    // }

    
    // if(Nvs_Read_String(STORAGE_KEY_IOT_HUB_HOST_NAME, IoTHubHandle.hostName) == true)
    // {
    //     ESP_LOGI("SYS INIT", "IOT_HUB_NAME: %s", IoTHubHandle.hostName);
    // }else{
    //     ESP_LOGE("SYS INIT", "Read IOT_HUB_NAME fail, set default: %s", SYS_IOT_HUB_HOST_NAME_DEFAULT);
    // }

    
    // if(Nvs_Read_String(STORAGE_KEY_IOT_HUB_DEVICE_ID, IoTHubHandle.deviceId) == true)
    // {
    //     ESP_LOGI("SYS INIT", "IOT_HUB_DEVICE_ID: %s", IoTHubHandle.deviceId);
    // }else{
    //     ESP_LOGE("SYS INIT", "Read IOT_HUB_DEVICE_ID fail, set default: %s", SYS_IOT_HUB_DEVICE_ID_DEFAULT);
    // }

    
    // if(Nvs_Read_String(STORAGE_KEY_IOT_HUB_SYMMETRIC_KEY, IoTHubHandle.symmetricKey) == true)
    // {
    //     ESP_LOGI("SYS INIT", "IOT_HUB_SYMM_KEY: %s", IoTHubHandle.symmetricKey);
    // }else{
    //     ESP_LOGE("SYS INIT", "Read IOT_HUB_SYMM_KEY fail, set default: %s", SYS_IOT_HUB_SYMMETRIC_KEY_DEFAULT);
    // }

    // memcpy(TCP_Handle.ssid, "HARDWARE", strlen("HARDWARE"));
    // memcpy(TCP_Handle.pass, "H@rdWar3.86", strlen("H@rdWar3.86"));

    // memcpy(TCP_Handle.ssid, "PETROCLOUD DEVICE", strlen("PETROCLOUD DEVICE"));
    // memcpy(TCP_Handle.pass, "PetroCl0ud.789", strlen("PetroCl0ud.789"));

    // memcpy(TCP_Handle.ssid, "Le Bien", strlen("Le Bien"));
    // memcpy(TCP_Handle.pass, "dang16072005", strlen("dang16072005"));

    // Keep STA credentials empty at boot; wifi_config_manager will load from FRAM when available.
    // memcpy(TCP_Handle.ssid, "Mebieco", strlen("Mebieco"));
    // memcpy(TCP_Handle.pass, "68686868@", strlen("68686868@"));
    // memcpy(TCP_Handle.ssid, "Le Khanh Dang", strlen("Le Khanh Dang"));
    // memcpy(TCP_Handle.pass, "0337726035", strlen("0337726035"));

    // memcpy(IoTHubHandle.hostName, "dev-iot-hub-1.azure-devices.net", strlen("dev-iot-hub-1.azure-devices.net"));
    // memcpy(IoTHubHandle.deviceId, "test-devices-1", strlen("test-devices-1"));
    // memcpy(IoTHubHandle.symmetricKey, "5UQFalxedSORhesnVEP2FIOehjtEA73N+e2jUEc9ewY=", strlen("5UQFalxedSORhesnVEP2FIOehjtEA73N+e2jUEc9ewY="));
    // memcpy(IoTHubHandle.hostName, "stg-iot-hub-1.azure-devices.net", strlen("stg-iot-hub-1.azure-devices.net"));
    // memcpy(IoTHubHandle.deviceId, "stg-devices-1", strlen("stg-devices-1"));
    // memcpy(IoTHubHandle.symmetricKey, "63TitmDHTVPp4FyR7VqsE38eTGgGNJghJrMM7QODEik=", strlen("63TitmDHTVPp4FyR7VqsE38eTGgGNJghJrMM7QODEik="));
    
    // memcpy(IoTHubHandle.hostName, "dev-iot-hub-1.azure-devices.net", strlen("dev-iot-hub-1.azure-devices.net"));
    // memcpy(IoTHubHandle.deviceId, "test-devices-1", strlen("test-devices-1"));
    // memcpy(IoTHubHandle.symmetricKey, "5UQFalxedSORhesnVEP2FIOehjtEA73N+e2jUEc9ewY=", strlen("5UQFalxedSORhesnVEP2FIOehjtEA73N+e2jUEc9ewY="));

    // // === BYPASS FLASH FOR TESTING: Use hardcoded values from menuconfig ===
    // ESP_LOGW("SYS INIT", "Testing Mode: Using hardcoded Azure credentials from menuconfig");
    // strncpy(IoTHubHandle.hostName, CONFIG_AZURE_IOT_HUB_FQDN, sizeof(IoTHubHandle.hostName) - 1);
    // strncpy(IoTHubHandle.deviceId, CONFIG_AZURE_IOT_DEVICE_ID, sizeof(IoTHubHandle.deviceId) - 1);
    // strncpy(IoTHubHandle.symmetricKey, CONFIG_AZURE_IOT_DEVICE_SYMMETRIC_KEY, sizeof(IoTHubHandle.symmetricKey) - 1);

    
    char az_host[64] = {0};
    char az_dev[64] = {0};
    char az_sym[64] = {0};

    if(azure_config_manager_load(az_host, sizeof(az_host), az_dev, sizeof(az_dev), az_sym, sizeof(az_sym)))
    {
        ESP_LOGI("SYS INIT", "Loaded Azure Config from FRAM: Host=%s, Device=%s", az_host, az_dev);
        strncpy(IoTHubHandle.hostName, az_host, sizeof(IoTHubHandle.hostName) - 1);
        strncpy(IoTHubHandle.deviceId, az_dev, sizeof(IoTHubHandle.deviceId) - 1);
        strncpy(IoTHubHandle.symmetricKey, az_sym, sizeof(IoTHubHandle.symmetricKey) - 1);
    }
    else
    {
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
