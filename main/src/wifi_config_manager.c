#include "wifi_config_manager.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "user_system.h"
#include "tcp_server_com.h"
#include "esp_mac.h"
#include "esp_http_server.h"

 

static char s_ap_ssid[32] = {0}; // Lưu SSID đã ghép
static const char *WIFI_CFG_TAG = "wifi_cfg";
static int s_retry_num = 0;
static bool s_allow_sta_connect = false;
static TaskHandle_t s_connect_task = NULL;
static TaskHandle_t s_slow_retry_task = NULL;
static char s_pending_ssid[32] = {0};
static char s_pending_pass[64] = {0};
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;

#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY
#define WIFI_SLOW_RETRY_INTERVAL_MS 10000

static void prv_slow_retry_task(void *pvParameters)
{
    (void)pvParameters;

    while(1)
    {
        if(s_allow_sta_connect && !Sys_Info.isWifiConnected)
        {
            ESP_LOGI(WIFI_CFG_TAG, "Slow retry connect...");
            esp_wifi_connect();
        }
        vTaskDelay(pdMS_TO_TICKS(WIFI_SLOW_RETRY_INTERVAL_MS));
    }
}

 
static void prv_wifi_connect(const char *ssid, const char *pass)
{
    wifi_config_t wifi_config = {0};

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    if(ssid != NULL)
    {
        strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    }
    if(pass != NULL)
    {
        strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
    }

    s_allow_sta_connect = false; // Disable auto-reconnect during config
    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_LOGI(WIFI_CFG_TAG, "Connecting to SSID: %s", (char *)wifi_config.sta.ssid);
    s_allow_sta_connect = true;  // Re-enable auto-reconnect
    s_retry_num = 0;             // Reset retry counter
    esp_wifi_connect();
}

// static void prv_wifi_restart(wifi_mode_t mode, const wifi_config_t *ap_config)
// {
//     esp_err_t stop_err = esp_wifi_stop();
//     if(stop_err != ESP_OK && stop_err != ESP_ERR_WIFI_NOT_STARTED)
//     {
//         ESP_ERROR_CHECK(stop_err);
//     }

//     ESP_ERROR_CHECK(esp_wifi_set_mode(mode));
// #if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
//     if(ap_config != NULL)
//     {
//         ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, ap_config));
//     }
// #endif
//     ESP_ERROR_CHECK(esp_wifi_start());

//     if((mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) && s_sta_netif != NULL)
//     {
//         esp_err_t dhcp_err = esp_netif_dhcpc_start(s_sta_netif);
//         if(dhcp_err != ESP_OK && dhcp_err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)
//         {
//             ESP_LOGW(WIFI_CFG_TAG, "DHCP start failed: %s", esp_err_to_name(dhcp_err));
//         }
//     }
// }
/**
 * @brief Xây dựng SSID duy nhất cho AP bằng cách ghép prefix + 3 byte cuối MAC.
 *        Kết quả lưu vào biến static s_ap_ssid.
 *        Ví dụ: "MEBICO_ESP32_1A2B3C"
 */
 static void prv_build_ap_ssid(void){
    uint8_t mac[6] = {0};

    // Đọc MAC address của interface Wifi AP
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (err != ESP_OK){
        ESP_LOGE(WIFI_CFG_TAG, "Failed to read MAC: %s", esp_err_to_name(err));
        // Fallback: dùng prefix không có MAC nếu lỗi
        strncpy(s_ap_ssid, WIFI_AP_SSID_PREFIX, sizeof(s_ap_ssid) - 1);
        return;
    }

    // Ghép prefix + 3 byte cuối MAC thành chuỗi HEX
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s%02X%02X%02X", WIFI_AP_SSID_PREFIX, mac[3], mac[4], mac[5]);
    ESP_LOGI(WIFI_CFG_TAG, "AP SSID built: %s", s_ap_ssid);

 }

static void prv_build_ap_config(wifi_config_t *ap_config)
{
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if(ap_config == NULL)
    {
        return;
    }
    memset(ap_config, 0, sizeof(*ap_config));
    strncpy((char *)ap_config->ap.ssid, s_ap_ssid, sizeof(ap_config->ap.ssid) - 1);
    strncpy((char *)ap_config->ap.password, WIFI_AP_PASS, sizeof(ap_config->ap.password) - 1);
    ap_config->ap.ssid_len = strlen(s_ap_ssid);
    ap_config->ap.channel = WIFI_AP_CHANNEL;
    ap_config->ap.max_connection = WIFI_AP_MAX_CONN;
    ap_config->ap.authmode = (strlen(WIFI_AP_PASS) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;
#else
    (void)ap_config;
#endif
}

static void prv_connect_task(void *pvParameters)
{
    (void)pvParameters;

    vTaskDelay(pdMS_TO_TICKS(300));

#if CONFIG_ESP_WIFI_STA_SUPPORT
    if(s_sta_netif != NULL)
    {
        esp_err_t dhcp_err = esp_netif_dhcpc_start(s_sta_netif);
        if(dhcp_err != ESP_OK && dhcp_err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)
        {
            ESP_LOGW(WIFI_CFG_TAG, "DHCP start failed: %s", esp_err_to_name(dhcp_err));
        }
    }
#endif
    prv_wifi_connect(s_pending_ssid, s_pending_pass);
    s_connect_task = NULL;
    vTaskDelete(NULL);
}

// static void prv_set_mode_ap_only(void)
// {
// #if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
// #else
//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
// #endif
// }

// static void prv_set_mode_apsta(void)
// {
// #if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
// #else
//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
// #endif
// }

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        if(s_allow_sta_connect)
        {
            esp_wifi_connect();
        }
    }
    else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        Sys_Info.isWifiConnected = false;
        if(s_allow_sta_connect)
        {
            if(s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
            {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(WIFI_CFG_TAG, "Fast retry %d/%d", s_retry_num, EXAMPLE_ESP_MAXIMUM_RETRY);
            }
            else
            {
                if(s_slow_retry_task == NULL)
                {
                    xTaskCreatePinnedToCore(prv_slow_retry_task, "wifi_slow_retry", 3072, NULL, 3, &s_slow_retry_task, 0);
                }
                ESP_LOGI(WIFI_CFG_TAG, "Switch to slow retry every %d s", WIFI_SLOW_RETRY_INTERVAL_MS/1000);
            }
        }
    }
    else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        s_retry_num = 0;
        Sys_Info.isWifiConnected = true;
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(WIFI_CFG_TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

bool wifi_config_manager_load(char *ssid_out, size_t ssid_len, char *pass_out, size_t pass_len)
{
    nvs_handle_t my_handle;
    esp_err_t err;
    bool success = false;

    err = nvs_open("sys_cfg", NVS_READONLY, &my_handle);
    if (err != ESP_OK) return false;

    if (ssid_out != NULL && ssid_len > 0) {
        size_t required_size = ssid_len;
        err = nvs_get_str(my_handle, "ssid", ssid_out, &required_size);
        if (err == ESP_OK) success = true;
    }

    if (pass_out != NULL && pass_len > 0) {
        size_t required_size = pass_len;
        err = nvs_get_str(my_handle, "pass", pass_out, &required_size);
    }

    nvs_close(my_handle);
    return success;
}

bool wifi_config_manager_save(const char *ssid, const char *pass)
{
    if(ssid == NULL || ssid[0] == '\0') {
        return false;
    }

    nvs_handle_t my_handle;
    esp_err_t err;

    err = nvs_open("sys_cfg", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return false;

    err = nvs_set_str(my_handle, "ssid", ssid);
    if (err == ESP_OK && pass != NULL) {
        err = nvs_set_str(my_handle, "pass", pass);
    }
    
    if (err == ESP_OK) {
        err = nvs_commit(my_handle);
    }
    nvs_close(my_handle);

    if (err != ESP_OK) return false;

    memset(TCP_Handle.ssid, 0, sizeof(TCP_Handle.ssid));
    memset(TCP_Handle.pass, 0, sizeof(TCP_Handle.pass));
    strncpy(TCP_Handle.ssid, ssid, sizeof(TCP_Handle.ssid) - 1);
    if (pass != NULL) {
        strncpy(TCP_Handle.pass, pass, sizeof(TCP_Handle.pass) - 1);
    }

    strncpy(s_pending_ssid, ssid, sizeof(s_pending_ssid) - 1);
    if (pass != NULL) {
        strncpy(s_pending_pass, pass, sizeof(s_pending_pass) - 1);
    }
    return true;
}

bool wifi_config_manager_clear(void)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("sys_cfg", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return false;

    nvs_erase_key(my_handle, "ssid");
    nvs_erase_key(my_handle, "pass");
    nvs_commit(my_handle);
    nvs_close(my_handle);
    return true;
}

bool azure_config_manager_load(char *host, size_t host_len, char *dev, size_t dev_len, char *sym, size_t sym_len)
{
    nvs_handle_t my_handle;
    esp_err_t err;
    bool success = false;

    err = nvs_open("sys_cfg", NVS_READONLY, &my_handle);
    if (err != ESP_OK) return false;

    if (host != NULL && host_len > 0) {
        size_t required_size = host_len;
        err = nvs_get_str(my_handle, "az_host", host, &required_size);
        if (err == ESP_OK) success = true;
    }
    if (dev != NULL && dev_len > 0) {
        size_t required_size = dev_len;
        err = nvs_get_str(my_handle, "az_dev", dev, &required_size);
        if (err == ESP_OK) success = true;
    }
    if (sym != NULL && sym_len > 0) {
        size_t required_size = sym_len;
        err = nvs_get_str(my_handle, "az_sym", sym, &required_size);
        if (err == ESP_OK) success = true;
    }

    nvs_close(my_handle);
    return success;
}

bool azure_config_manager_save(const char *host, const char *dev, const char *sym)
{
    if (host == NULL || dev == NULL || sym == NULL) return false;

    nvs_handle_t my_handle;
    esp_err_t err;

    err = nvs_open("sys_cfg", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return false;

    err = nvs_set_str(my_handle, "az_host", host);
    if (err == ESP_OK) err = nvs_set_str(my_handle, "az_dev", dev);
    if (err == ESP_OK) err = nvs_set_str(my_handle, "az_sym", sym);
    
    if (err == ESP_OK) {
        nvs_commit(my_handle);
    }
    nvs_close(my_handle);

    return (err == ESP_OK);
}

bool azure_config_manager_clear(void)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("sys_cfg", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return false;

    nvs_erase_key(my_handle, "az_host");
    nvs_erase_key(my_handle, "az_dev");
    nvs_erase_key(my_handle, "az_sym");
    nvs_commit(my_handle);
    nvs_close(my_handle);
    return true;
}

void wifi_config_manager_schedule_connect(void)
{
    if(s_connect_task != NULL)
    {
        return;
    }

    xTaskCreatePinnedToCore(prv_connect_task, "wifi_connect_task", 4096, NULL, 4, &s_connect_task, 0);
}

void wifi_config_manager_prepare_scan(void)
{
    // No-op: keep AP running to avoid dropping HTTP connection during scan.
}

bool wifi_config_manager_init(void)
{
    esp_err_t err;

    err = esp_netif_init();
    if(err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(WIFI_CFG_TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_event_loop_create_default();
    if(err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(WIFI_CFG_TAG, "event_loop_create_default failed: %s", esp_err_to_name(err));
        return false;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();

#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    s_ap_netif = esp_netif_create_default_wifi_ap();
    esp_netif_ip_info_t ap_ip_info;
    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_set_ip4_addr(&ap_ip_info.ip, 192,168,14,1);
    esp_netif_set_ip4_addr(&ap_ip_info.gw, 192, 168, 14, 1);
    esp_netif_set_ip4_addr(&ap_ip_info.netmask, 255, 255, 255, 0);
    esp_netif_set_ip_info(s_ap_netif, &ap_ip_info);
    esp_netif_dhcps_start(s_ap_netif);
#else
    ESP_LOGW(WIFI_CFG_TAG, "SoftAP support disabled in sdkconfig");
#endif

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    prv_build_ap_ssid();
    wifi_config_t ap_config;
    prv_build_ap_config(&ap_config);

    char ssid[32] = {0};
    char pass[64] = {0};

    // // === BYPASS FLASH FOR TESTING: Use hardcoded default WiFi ===
    // ESP_LOGW(WIFI_CFG_TAG, "Testing Mode: Using hardcoded WiFi credentials");
    // strncpy(ssid, SYS_WIFI_SSID_DEFAULT, sizeof(ssid) - 1);
    // strncpy(pass, SYS_WIFI_PASS_DEFAULT, sizeof(pass) - 1);
    // bool has_saved = true;


    bool has_saved = wifi_config_manager_load(ssid, sizeof(ssid), pass, sizeof(pass)) && (ssid[0] != '\0');


    s_allow_sta_connect = has_saved ? true : false;

#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
#else
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
#endif
    ESP_ERROR_CHECK(esp_wifi_start());

    if(s_sta_netif != NULL)
    {
        esp_err_t dhcp_err = esp_netif_dhcpc_start(s_sta_netif);
        if(dhcp_err != ESP_OK && dhcp_err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)
        {
            ESP_LOGW(WIFI_CFG_TAG, "DHCP start failed: %s", esp_err_to_name(dhcp_err));
        }
    }

    if(has_saved)
    {
        strncpy(TCP_Handle.ssid, ssid, sizeof(TCP_Handle.ssid) - 1);
        strncpy(TCP_Handle.pass, pass, sizeof(TCP_Handle.pass) - 1);
        prv_wifi_connect(ssid, pass);
        ESP_LOGI(WIFI_CFG_TAG, "Saved WiFi found, AP+STA enabled");
    }
    else
    {
        ESP_LOGI(WIFI_CFG_TAG, "No saved WiFi, AP only");
    }

    ESP_LOGI(WIFI_CFG_TAG, "AP started SSID:%s", s_ap_ssid);
    return true;
}

/* ============================================================
 *  Shared Secret Authentication
 * ============================================================ */

bool auth_secret_load(char *out, size_t out_len){
    if (out == NULL || out_len == 0) return false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open("sys_cfg", NVS_READONLY, &handle);
    if (err != ESP_OK){
        /* NVS lỗi -> dùng default */
        strncpy(out, AUTH_SECRET_DEFAULT, out_len - 1);
        out[out_len - 1] = '\0';
        return true;
    }

    size_t sz = out_len;
    err = nvs_get_str(handle, "auth_sec", out, &sz);
    nvs_close(handle);
    if (err != ESP_OK){
        /* Chưa có trong NVS -> dùng default */
        strncpy(out, AUTH_SECRET_DEFAULT, out_len - 1);
        out[out_len - 1] = '\0';
    }
    return true;
}

bool auth_secret_save(const char *secret){
    if (secret == NULL || strlen(secret) == 0) return false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open("sys_cfg", NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;

    err = nvs_set_str(handle, "auth_sec", secret);
    if (err == ESP_OK) nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(WIFI_CFG_TAG, "Auth secret updated");
    return (err == ESP_OK);
}

bool auth_secret_verify(const char *token){
    if (token == NULL) return false;

    char stored[AUTH_SECRET_MAX_LEN] = {0};
    auth_secret_load(stored, sizeof(stored));

    return (strcmp(token, stored) == 0);
}

/* ============================================================
 *  WiFi change with automatic fallback
 * ============================================================ */
 bool wifi_config_manager_set_with_fallback(const char *new_ssid, const char *new_pass, uint32_t timeout_ms){
    // Bước 1: Backup Wifi cũ
    char old_ssid[32] = {0};
    char old_pass[64] = {0};
    bool had_old = wifi_config_manager_load(old_ssid, sizeof(old_ssid), old_pass, sizeof(old_pass)) && (old_ssid[0] != '\0');

    // Bước 2: Ghi Wifi mới
    if (!wifi_config_manager_save(new_ssid, new_pass)){
        ESP_LOGE(WIFI_CFG_TAG, "Fallback: save new WIFI failed");
        return false;
    }

    // Bước 3: Cố gắng kết nối lại
    Sys_Info.isWifiConnected = false;
    wifi_config_manager_schedule_connect();

    // Bước 4: Chờ đợi kết nối mới
    uint32_t elapsed = 0;
    const uint32_t poll_ms = 500;

    while (elapsed < timeout_ms){
        vTaskDelay(pdMS_TO_TICKS(poll_ms));
        elapsed += poll_ms;
        if(Sys_Info.isWifiConnected){
            ESP_LOGI(WIFI_CFG_TAG, "Fallback: new Wifi OK after %lu ms", (unsigned long)elapsed);
            return true;
        }
    }

    // Bước 5: TIMEOUT + Rollback
    ESP_LOGW(WIFI_CFG_TAG, "Fallback: TIMEOUT, rolling back...");

    if (had_old){
        wifi_config_manager_save(old_ssid, old_pass);
        wifi_config_manager_schedule_connect();

        elapsed = 0;
        while(elapsed < timeout_ms){
            vTaskDelay(pdMS_TO_TICKS(poll_ms));
            elapsed += poll_ms;
            if(Sys_Info.isWifiConnected){
                ESP_LOGI(WIFI_CFG_TAG, "Fallback: old WiFi restored after %lu ms",
                         (unsigned long)elapsed);
                return false;
            }
        }
        ESP_LOGE(WIFI_CFG_TAG, "Fallback: old Wifi also failed. SoftAP active");
    }
    else{
        ESP_LOGW(WIFI_CFG_TAG, "Fallback: no old WiFi. SoftAP active.");
    }
    return false;
 }

 bool is_authenticated(httpd_req_t *req){
    char cookie_buf[128] = {0};
    // Đọc trường "Cookie" từ header của request
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_buf, sizeof(cookie_buf)) == ESP_OK){
        // Tìm chuỗi "session = " trong cookie
        char *session = strstr(cookie_buf, "session=");
        if (session){
            session += 8; // Di chuyển con trỏ qua chuỗi "session="

            char token[AUTH_SECRET_MAX_LEN] = {0};
            int i = 0;
            // Trích xuất giá trị token cho đến dấu ; hoặc khoảng trắng hoặc hết chuỗi
            while(session[i] && session[i] != ';' && session[i] != ' ' && i < sizeof(token) - 1){
                token[i] = session[i];
                i++;
            }
            token[i] = '\0';
            return auth_secret_verify(token);
        }
    }
    return false;
 }    
