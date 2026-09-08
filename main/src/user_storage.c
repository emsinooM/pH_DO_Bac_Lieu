#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "user_storage.h"
#include "user_system.h"

#define STORAGE_NAMESPACE "storage"
static const char *TAG = "NVS_STORAGE";


bool Nvs_Write_String(const char *key, const char *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    bool result = false;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS WRITE: Error (%s) opening NVS handle!", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set string failed for key '%s': %s", key, esp_err_to_name(err));
    } else {
        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Commit failed: %s", esp_err_to_name(err));
        } else {
            result = true;
        }
    }
    nvs_close(handle);
    return result;
}

bool Nvs_Write_Number(const char *key, uint32_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    bool result = false;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_u32(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set u32 failed for key '%s': %s", key, esp_err_to_name(err));
    } else {
        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Commit failed: %s", esp_err_to_name(err));
        } else {
            result = true;
        }
    }
    nvs_close(handle);
    return result;
}

bool Nvs_Read_String(const char *key, char *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return false;
    }
    bool result = false;
    size_t strSize = 0;
    err = nvs_get_str(handle, key, NULL, &strSize);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Get size fail for key '%s': %s", key, esp_err_to_name(err));
    } else {
        err = nvs_get_str(handle, key, value, &strSize);
        if (err == ESP_OK) {
            result = true;
        } else {
            ESP_LOGE(TAG, "Read string fail: %s", esp_err_to_name(err));
        }
    }
    nvs_close(handle);
    return result;
}

bool Nvs_Read_Number(const char *key, uint32_t *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return false;
    }
    bool result = false;
    err = nvs_get_u32(handle, key, value);
    if (err == ESP_OK) {
        result = true;
    } else {
        ESP_LOGW(TAG, "Get u32 fail for key '%s': %s", key, esp_err_to_name(err));
    }
    nvs_close(handle);
    return result;
}

void Nvs_Storage_Task(void)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

