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
#include "esp_timer.h"
#include "user_azure.h"
#include "user_storage.h"


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

    static char updateUrl[512] = {0};
    memset(updateUrl, 0, sizeof(updateUrl));
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
    config.buffer_size_tx = 2048;
    config.buffer_size = 8192; // Tăng buffer từ 1KB lên 8KB để nạp nhanh gấp 4-8 lần trong RAM sạch

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    ESP_LOGI("OTA", "Attempting to download update from %s", config.url);
    ESP_LOGW("OTA", "Free heap: %lu, Min free heap: %lu",
         (unsigned long)esp_get_free_heap_size(),
         (unsigned long)esp_get_minimum_free_heap_size());

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE("OTA", "esp_https_ota_begin failed (%s)", esp_err_to_name(err));
        strncpy(g_ota_err_desc, "OTA Begin failed", sizeof(g_ota_err_desc) - 1);
        return ESP_FAIL;
    }

    int total_len = esp_https_ota_get_image_size(https_ota_handle);
    int last_pct = -1;

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        int len_read = esp_https_ota_get_image_len_read(https_ota_handle);
        if (total_len > 0) {
            int pct = (len_read * 100) / total_len;
            if (pct != last_pct && (pct % 10 == 0 || pct == 100)) {
                last_pct = pct;
                ESP_LOGI("OTA", "Progress: %d%% (%d / %d bytes)", pct, len_read, total_len);
            }
        }
    }

    if (err == ESP_OK) {
        err = esp_https_ota_finish(https_ota_handle);
        if (err == ESP_OK) {
            ESP_LOGI("OTA", "OTA Succeed!");
            return ESP_OK;
        }
    } else {
        esp_https_ota_abort(https_ota_handle);
    }

    ESP_LOGE("OTA", "Firmware upgrade failed (%s)", esp_err_to_name(err));
    strncpy(g_ota_err_desc, "HTTP/Flash error", sizeof(g_ota_err_desc) - 1);
    return ESP_FAIL;

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

bool User_Ota_Check_And_Run_Pending(void)
{
    char ota_pending[8] = {0};
    if (!Nvs_Read_String("ota_pending", ota_pending) || strcmp(ota_pending, "1") != 0) {
        return false; // Không có OTA pending
    }

    char url[512] = {0};
    if (!Nvs_Read_String("ota_url", url) || strlen(url) == 0) {
        ESP_LOGE("OTA", "Co co ota_pending nhung ota_url rong! Huy bo OTA.");
        Nvs_Write_String("ota_pending", "0");
        return false;
    }

    // Xóa cờ ota_pending ngay lập tức trong NVS để tránh lặp vô tận nếu nạp bị lỗi nặng
    Nvs_Write_String("ota_pending", "0");

    ESP_LOGW("OTA", "=== PHÁT HIỆN OTA PENDING - BẮT ĐẦU NẠP FIRMWARE TRONG RAM SẠCH ===");
    ESP_LOGW("OTA", "URL: %s", url);
    ESP_LOGI("OTA", "Free Heap khi bat dau OTA: %lu bytes", (unsigned long)esp_get_free_heap_size());

    g_ota_status = OTA_STATUS_DOWNLOADING;
    esp_err_t ret = update_firmware(url);
    if (ret == ESP_OK)
    {
        g_ota_status = OTA_STATUS_SUCCESS;
        Nvs_Write_String("ota_res", "success");
        Nvs_Write_String("ota_err", "None");
        ESP_LOGI("OTA", "OTA Thanh cong! Dang khoi dong lai vao Firmware moi trong 2s...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }
    else
    {
        g_ota_status = OTA_STATUS_FAILED;
        if (strlen(g_ota_err_desc) == 0) {
            strncpy(g_ota_err_desc, "Download failed", sizeof(g_ota_err_desc) - 1);
        }
        Nvs_Write_String("ota_res", "failed");
        Nvs_Write_String("ota_err", g_ota_err_desc);
        ESP_LOGE("OTA", "Firmware upgrade That bai! Khoi dong lai trong 3s...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    return true;
}

void User_Ota_Task(void *pvParameters)
{
    User_Ota_Check_And_Run_Pending();
    vTaskDelete(NULL);
}

static void prv_reboot_timer_cb(void *arg) {
    ESP_LOGW("OTA", "Thuc hien esp_restart() theo quy trinh Reboot-to-OTA...");
    esp_restart();
}

/* Hàm kích hoạt OTA từ bên ngoài (Web / Azure Direct Method) */
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

    // Lưu URL và cờ ota_pending vào NVS
    Nvs_Write_String("ota_url", url);
    Nvs_Write_String("ota_pending", "1");
    
    ESP_LOGW("OTA", "Da ghi NVS: ota_pending=1, URL: %s. Thiet bi se tu dong esp_restart() sau 1.5s!", url);

    // Tạo esp_timer restart sau 1.5s để cho phép response Direct Method / HTTP Server gửi hoàn tất
    const esp_timer_create_args_t timer_args = {
        .callback = &prv_reboot_timer_cb,
        .name = "ota_reboot_timer"
    };
    esp_timer_handle_t reboot_timer = NULL;
    if (esp_timer_create(&timer_args, &reboot_timer) == ESP_OK) {
        esp_timer_start_once(reboot_timer, 1500000); // 1.5 giây (1,500,000 microseconds)
    } else {
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
    }
}






