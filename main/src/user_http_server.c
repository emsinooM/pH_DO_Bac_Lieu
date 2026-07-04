#include "user_http_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_check.h"
#include <stdlib.h>
#include <sys/param.h>
#include "user_system.h"
#include "mdns.h"
#include "cJSON.h"
#include "time.h"
#include "web_portal.h"

static const char *TAG = "http_server";

esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (strcmp("/echo", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/echo URI is not available");
        return ESP_FAIL;
    }
    
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not Found");
    return ESP_FAIL;
}

static httpd_handle_t start_webserver(void)
{
    esp_err_t err = mdns_init();
    if (err) {
        ESP_LOGW("DNS: ", "mDNS init failed: %d (continue without mDNS)", err);
    } else {
        mdns_hostname_set("esp32server");
    }

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.lru_purge_enable = true;
    config.max_open_sockets = 5;
    config.max_uri_handlers = 16;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    esp_err_t start_err = httpd_start(&server, &config);
    if (start_err == ESP_OK) 
    {
        ESP_LOGI(TAG, "Registering Web Portal handlers");
        web_portal_register_handlers(server);

        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);

        return server;
    }

    ESP_LOGE(TAG, "Error starting server: %s", esp_err_to_name(start_err));
    return NULL;
}

void User_Http_Server_Task(void)
{
    ESP_LOGI("HTTP SERVER: ", "Start http server task");
    start_webserver();

    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
