#include "user_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include <string.h>
#include "esp_crt_bundle.h"
#include "user_azure.h"

EventGroupHandle_t otaEventGroup;
static bool s_ota_use_auth_header = true;

user_ota_status_t g_ota_status = OTA_STATUS_IDLE;
char g_ota_err_desc[64] = {0};

const char* User_Ota_Get_Status_String(void)
{
    switch(g_ota_status)
    {
        case OTA_STATUS_IDLE:
            return "Idle";
        case OTA_STATUS_WAIT_AZURE:
            return "Waiting for services cleanup...";
        case OTA_STATUS_DOWNLOADING:
            return "Downloading and Flashing firmware...";
        case OTA_STATUS_SUCCESS:
            return "Update Successful! Rebooting...";
        case OTA_STATUS_FAILED:
            if (strlen(g_ota_err_desc) > 0) {
                static char err_buf[128];
                snprintf(err_buf, sizeof(err_buf), "Update Failed: %s. Rebooting...", g_ota_err_desc);
                return err_buf;
            }
            return "Update Failed! Rebooting...";
        default:
            return "Unknown";
    }
}


esp_err_t IRAM_ATTR _http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_CONNECTED)
    {
        if (s_ota_use_auth_header) {
            // esp_http_client_set_header(evt->client, "Authorization", "Bearer your_token");
            esp_http_client_set_header(evt->client, "Authorization", "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJUZW5hbnRDb2RlIjoicHZvaWwiLCJodHRwOi8vc2NoZW1hcy5taWNyb3NvZnQuY29tL3dzLzIwMDgvMDYvaWRlbnRpdHkvY2xhaW1zL3JvbGUiOiJEZXZpY2UiLCJVc2VyTmFtZSI6InBlY28iLCJuYmYiOjE2NDQ1NTIxOTcsImV4cCI6MTcwNzY2NjAxNywiaXNzIjoiaHR0cDovL3NtYXJ0cGV0cm8uaW8vIiwiYXVkIjoiU21hcnRQZXRybyJ9.03hQ3zdz3YJO-y8lfYV805qhapYts1iwdHkwVR-skms");
        }
    }
    return ESP_OK;
}


static bool prvBuildOtaUrl(const char *updateFileName, char *out, size_t out_len)
{
    if(updateFileName == NULL || out == NULL || out_len == 0)
    {
        return false;
    }

    static const char *base_url = "https://shrimpiotdblobs.blob.core.windows.net";

    if(updateFileName[0] == '/')
    {
        return (snprintf(out, out_len, "%s%s", base_url, updateFileName) > 0);
    }

    return (snprintf(out, out_len, "%s/%s", base_url, updateFileName) > 0);
}

esp_err_t update_firmware(const char *updateFileName)
{
    if(updateFileName == NULL)
    {
        strncpy(g_ota_err_desc, "Null URL", sizeof(g_ota_err_desc) - 1);
        return ESP_FAIL;
    }

    char updateUrl[512] = {0};
    if((strncmp(updateFileName, "http://", 7) == 0) || (strncmp(updateFileName, "https://", 8) == 0))
    {
        if(snprintf(updateUrl, sizeof(updateUrl), "%s", updateFileName) <= 0)
        {
            strncpy(g_ota_err_desc, "Invalid URL string", sizeof(g_ota_err_desc) - 1);
            return ESP_FAIL;
        }
    }
    else if(!prvBuildOtaUrl(updateFileName, updateUrl, sizeof(updateUrl)))
    {
        strncpy(g_ota_err_desc, "Build URL failed", sizeof(g_ota_err_desc) - 1);
        return ESP_FAIL;
    }

    printf("URL: %s\n", updateUrl);
    s_ota_use_auth_header = (strstr(updateUrl, "sig=") == NULL);

    esp_http_client_config_t config = {0}; 

    config.url = updateUrl;
    config.event_handler = _http_event_handler;
    config.cert_pem = NULL;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.skip_cert_common_name_check = true;
    config.keep_alive_enable = false;
    config.timeout_ms = 15000;
    config.buffer_size_tx = 1024;
    config.buffer_size = 1024;
    // config.addr_type = AF_INET;

    // esp_http_client_config_t config = {
    //     .url = _url,
    //     .cert_pem = NULL,
    //     .keep_alive_enable = true,
    //     .skip_cert_common_name_check = true,
    //     .buffer_size_tx = 1024,
    //     .event_handler = _http_event_handler
    // };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    
    ESP_LOGI("OTA", "Attempting to download update from %s", config.url);
    ESP_LOGW("OTA", "Free heap: %lu, Min free heap: %lu",
         (unsigned long)esp_get_free_heap_size(),
         (unsigned long)esp_get_minimum_free_heap_size());

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK)
    {
        ESP_LOGI("OTA", "OTA Succeed");
    } 
    else
    {
        ESP_LOGE("OTA", "Firmware upgrade failed");
        strncpy(g_ota_err_desc, "HTTP/Flash error", sizeof(g_ota_err_desc) - 1);
        ret = ESP_FAIL;
    }

    return ret;
}



char g_ota_update_url[512] = {0};
// void User_Ota_Task(void)
// {
//     otaEventGroup = xEventGroupCreate();
//     while(1)
//     {
//         EventBits_t otaWaitBits = xEventGroupWaitBits(otaEventGroup, OTA_WAIT_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

//         if(otaWaitBits & OTA_WAIT_BIT)
//         {
//             ESP_LOGI("OTA", "OTA request received. Waiting for Azure to fully disconnect...");
    
//             /* Chờ cho đến khi Azure thực sự deinit (không chỉ delay cố định) */
//             int wait_count = 0;
//             while(IoTHubHandle.isAzureInitialized && wait_count < 30) {
//                 vTaskDelay(pdMS_TO_TICKS(500));
//                 wait_count++;
//                 ESP_LOGI("OTA", "Waiting... Azure still initialized (%d/30)", wait_count);
//             }
            
//             /* Thêm delay nhỏ cho TLS cleanup hoàn tất */
//             vTaskDelay(pdMS_TO_TICKS(1000));

//             ESP_LOGI("OTA", "Free heap after Azure disconnect: %lu bytes", 
//                     (unsigned long)esp_get_free_heap_size());

//             /* Xóa các Azure sub-tasks để giải phóng ~32KB stack memory */
//             User_Azure_Cleanup_For_OTA();

//             ESP_LOGI("OTA", "Free heap after cleanup: %lu bytes", 
//                     (unsigned long)esp_get_free_heap_size());
            
//             /* Kiểm tra heap trước khi bắt đầu - cần ~60KB cho TLS + OTA */
//             if(esp_get_free_heap_size() < 60000) {
//                 ESP_LOGE("OTA", "Not enough heap (%lu) even after cleanup. Rebooting...",
//                         (unsigned long)esp_get_free_heap_size());
//                 esp_restart();
//             }

//             ESP_LOGI("OTA", "Activating firmware download via HTTPS: %s", g_ota_update_url);
//             esp_err_t ret = update_firmware(g_ota_update_url);
//             if(ret == ESP_OK)
//             {
//                 ESP_LOGI("OTA", "OTA Succeed, Rebooting...");
//                 esp_restart();
//             }
//             else
//             {
//                 ESP_LOGE("OTA", "Firmware upgrade failed! Rebooting.");
//                 esp_restart();
//             }
//         }
//     }
// }

void User_Ota_Task(void *pvParameters)
{
    ESP_LOGW("OTA", "=== DYNAMIC OTA TASK STARTED ===");
    g_ota_status = OTA_STATUS_WAIT_AZURE;
    ESP_LOGI("OTA", "Waiting for Azure to fully disconnect...");
    
    /* Chờ cho đến khi Azure thực sự deinit hoàn tất */
    int wait_count = 0;
    while(IoTHubHandle.isAzureInitialized && wait_count < 30) {
        vTaskDelay(pdMS_TO_TICKS(500));
        wait_count++;
        ESP_LOGI("OTA", "Waiting... Azure still initialized (%d/30)", wait_count);
    }
    
    /* Thêm delay nhỏ cho TLS cleanup hoàn tất */
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI("OTA", "Free heap after Azure disconnect: %lu bytes", 
            (unsigned long)esp_get_free_heap_size());

    /* Xóa các Azure sub-tasks để giải phóng ~32KB stack memory của Azure */
    User_Azure_Cleanup_For_OTA();

    ESP_LOGI("OTA", "Free heap after cleanup: %lu bytes", 
            (unsigned long)esp_get_free_heap_size());
    
    /* Kiểm tra Heap trước khi tải: Cần tối thiểu 60KB cho mbedTLS + OTA Client */
    if(esp_get_free_heap_size() < 60000) {
        ESP_LOGE("OTA", "Not enough heap (%lu) even after cleanup. Rebooting...",
                (unsigned long)esp_get_free_heap_size());
        g_ota_status = OTA_STATUS_FAILED;
        strncpy(g_ota_err_desc, "Out of memory", sizeof(g_ota_err_desc) - 1);
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    g_ota_status = OTA_STATUS_DOWNLOADING;
    ESP_LOGW("OTA", "Activating firmware download via HTTPS: %s", g_ota_update_url);
    esp_err_t ret = update_firmware(g_ota_update_url);
    if(ret == ESP_OK)
    {
        g_ota_status = OTA_STATUS_SUCCESS;
        ESP_LOGI("OTA", "OTA Succeed, Rebooting in 2s...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }
    else
    {
        g_ota_status = OTA_STATUS_FAILED;
        if (strlen(g_ota_err_desc) == 0) {
            strncpy(g_ota_err_desc, "Download failed", sizeof(g_ota_err_desc) - 1);
        }
        ESP_LOGE("OTA", "Firmware upgrade failed! Restarting system in 3s...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }
    
    // Nếu tiến trình thất bại mà không restart, tự hủy task động này để giải phóng hoàn toàn Stack
    vTaskDelete(NULL);
}

/* Hàm kích hoạt OTA động từ bên ngoài */
void User_Ota_Trigger(const char *url)
{
    if (url == NULL || strlen(url) == 0) {
        ESP_LOGE("OTA", "Trigger failed: URL is empty");
        g_ota_status = OTA_STATUS_FAILED;
        strncpy(g_ota_err_desc, "Empty URL", sizeof(g_ota_err_desc) - 1);
        return;
    }
    
    // Clear old errors and set initial status
    memset(g_ota_err_desc, 0, sizeof(g_ota_err_desc));
    g_ota_status = OTA_STATUS_WAIT_AZURE;
    
    // 1. Lưu URL vào biến toàn cục g_ota_update_url
    memset(g_ota_update_url, 0, sizeof(g_ota_update_url));
    strncpy(g_ota_update_url, url, sizeof(g_ota_update_url) - 1);
    
    // 2. Kích hoạt cờ báo hiệu OTA để tạm thời đóng kết nối Azure và các task con
    bIsOtaActivated = true;
    IoTHubHandle.isNeedReinit = true;
    
    // 3. Khởi tạo Dynamic OTA Task với stack 8 KB (8192 bytes)
    // Task này sẽ tự động biến mất khỏi RAM sau khi chạy xong
    xTaskCreatePinnedToCore(User_Ota_Task, "OTA_Task_Dynamic", 8192, NULL, 5, NULL, 1);
}




