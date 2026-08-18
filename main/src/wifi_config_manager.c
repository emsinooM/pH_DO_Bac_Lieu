#include "wifi_config_manager.h"
#include <stdlib.h>
#include <string.h>
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
#include "esp_mac.h"
#include "esp_http_server.h"
#include "mdns.h"

 

static char s_ap_ssid[32] = {0}; // Lưu SSID đã ghép
static const char *WIFI_CFG_TAG = "wifi_cfg";
static int s_retry_num = 0;
static bool s_allow_sta_connect = false;
static bool s_was_allow_sta_connect = false;
static TaskHandle_t s_connect_task = NULL;
static TaskHandle_t s_slow_retry_task = NULL;
static char s_pending_ssid[32] = {0};
static char s_pending_pass[64] = {0};
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;

#define MAX_SCAN_APS 16
static wifi_ap_record_t s_scan_records[MAX_SCAN_APS];
static uint16_t s_scan_count = 0;
static wifi_scan_state_t s_scan_state = WIFI_SCAN_STATE_IDLE;
static TaskHandle_t s_scan_task_handle = NULL;


#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY
#define WIFI_SLOW_RETRY_INTERVAL_MS 10000

static void prv_align_ap_channel_with_sta(void)
{
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    uint8_t primary = 0;
    wifi_second_chan_t second;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK && primary > 0)
    {
        wifi_config_t ap_config;
        if (esp_wifi_get_config(WIFI_IF_AP, &ap_config) == ESP_OK)
        {
            if (ap_config.ap.channel != primary)
            {
                ESP_LOGI(WIFI_CFG_TAG, "Aligning SoftAP channel (%d -> %d) to match Router STA channel",
                         ap_config.ap.channel, primary);
                ap_config.ap.channel = primary;
                esp_wifi_set_config(WIFI_IF_AP, &ap_config);
            }
        }
    }
#endif
}

static bool prv_load_wifi_list(saved_wifi_list_t *list_out)
{
    if (list_out == NULL) return false;
    memset(list_out, 0, sizeof(saved_wifi_list_t));

    nvs_handle_t handle;
    esp_err_t err = nvs_open("sys_cfg", NVS_READONLY, &handle);
    if (err != ESP_OK) return false;

    size_t required_size = sizeof(saved_wifi_list_t);
    err = nvs_get_blob(handle, "wifi_list", list_out, &required_size);
    nvs_close(handle);

    if (err == ESP_OK && list_out->count > 0 && list_out->count <= MAX_SAVED_WIFI) {
        return true;
    }

    // Migration / Fallback: Try legacy single "ssid" & "pass" keys
    char legacy_ssid[32] = {0};
    char legacy_pass[64] = {0};
    err = nvs_open("sys_cfg", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t s_sz = sizeof(legacy_ssid);
        size_t p_sz = sizeof(legacy_pass);
        esp_err_t s_err = nvs_get_str(handle, "ssid", legacy_ssid, &s_sz);
        nvs_get_str(handle, "pass", legacy_pass, &p_sz);
        nvs_close(handle);

        if (s_err == ESP_OK && legacy_ssid[0] != '\0') {
            ESP_LOGI(WIFI_CFG_TAG, "Migrating legacy NVS wifi credentials (%s) to wifi_list blob...", legacy_ssid);
            strncpy(list_out->items[0].ssid, legacy_ssid, sizeof(list_out->items[0].ssid) - 1);
            strncpy(list_out->items[0].pass, legacy_pass, sizeof(list_out->items[0].pass) - 1);
            list_out->count = 1;

            if (nvs_open("sys_cfg", NVS_READWRITE, &handle) == ESP_OK) {
                nvs_set_blob(handle, "wifi_list", list_out, sizeof(saved_wifi_list_t));
                nvs_commit(handle);
                nvs_close(handle);
            }
            return true;
        }
    }

    return false;
}

static bool prv_save_wifi_list(const saved_wifi_list_t *list)
{
    if (list == NULL) return false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("sys_cfg", NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;

    err = nvs_set_blob(handle, "wifi_list", list, sizeof(saved_wifi_list_t));
    if (err == ESP_OK) {
        if (list->count > 0 && list->items[0].ssid[0] != '\0') {
            nvs_set_str(handle, "ssid", list->items[0].ssid);
            nvs_set_str(handle, "pass", list->items[0].pass);
        }
        nvs_commit(handle);
    }
    nvs_close(handle);
    return (err == ESP_OK);
}

static void prv_slow_retry_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t slow_count = 0;

    while(1)
    {
        if(s_allow_sta_connect && !Sys_Info.isWifiConnected)
        {
            slow_count++;
            
            s_allow_sta_connect = false;
            esp_wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(300));

            // Quét và tìm kiếm trong danh sách các Wi-Fi đã lưu ở NVS
            saved_wifi_list_t *saved_list = calloc(1, sizeof(saved_wifi_list_t));
            if (saved_list != NULL)
            {
                if (prv_load_wifi_list(saved_list) && saved_list->count > 0)
                {
                    ESP_LOGI(WIFI_CFG_TAG, "Multi-AP Fallback: Scanning for %d saved Wi-Fi networks...", saved_list->count);
                    
                    wifi_config_manager_trigger_scan();
                    int wait_cnt = 0;
                    while (wifi_config_manager_get_scan_state() == WIFI_SCAN_STATE_SCANNING && wait_cnt < 40)
                    {
                        vTaskDelay(pdMS_TO_TICKS(100));
                        wait_cnt++;
                    }

                    if (wifi_config_manager_get_scan_state() == WIFI_SCAN_STATE_DONE)
                    {
                        wifi_ap_record_t *ap_records = calloc(MAX_SCAN_APS, sizeof(wifi_ap_record_t));
                        if (ap_records != NULL)
                        {
                            uint16_t num_ap = wifi_config_manager_get_scan_results(ap_records, MAX_SCAN_APS);

                            if (num_ap > 0) {
                                int best_match_idx = -1;
                                int best_rssi = -100;
                                
                                for (uint16_t i = 0; i < num_ap; i++) {
                                    for (uint8_t j = 0; j < saved_list->count; j++) {
                                        if (strcmp((char *)ap_records[i].ssid, saved_list->items[j].ssid) == 0) {
                                            if (ap_records[i].rssi >= -85 && ap_records[i].rssi > best_rssi) {
                                                best_rssi = ap_records[i].rssi;
                                                best_match_idx = j;
                                            }
                                        }
                                    }
                                }
                                
                                if (best_match_idx >= 0) {
                                    ESP_LOGI(WIFI_CFG_TAG, "Fallback found known Wi-Fi AP: '%s' (RSSI: %d dBm). Switching target...",
                                             saved_list->items[best_match_idx].ssid, best_rssi);
                                    strncpy(s_pending_ssid, saved_list->items[best_match_idx].ssid, sizeof(s_pending_ssid) - 1);
                                    strncpy(s_pending_pass, saved_list->items[best_match_idx].pass, sizeof(s_pending_pass) - 1);

                                    if (best_match_idx > 0) {
                                        saved_wifi_item_t temp = saved_list->items[best_match_idx];
                                        for (int i = best_match_idx; i > 0; i--) {
                                            saved_list->items[i] = saved_list->items[i - 1];
                                        }
                                        saved_list->items[0] = temp;
                                        prv_save_wifi_list(saved_list);
                                        ESP_LOGI(WIFI_CFG_TAG, "Promoted fallback SSID '%s' to primary saved Wi-Fi in NVS", s_pending_ssid);
                                    }
                                } else {
                                    ESP_LOGW(WIFI_CFG_TAG, "Fallback scan found no known AP with RSSI >= -85dBm");
                                }
                            }
                            free(ap_records);
                        }
                    }
                    else
                    {
                        ESP_LOGE(WIFI_CFG_TAG, "Fallback scan failed or timed out");
                    }
                }
                free(saved_list);
            }



            ESP_LOGI(WIFI_CFG_TAG, "Slow retry connect (%lu) to SSID: %s...", (unsigned long)slow_count, s_pending_ssid);

            wifi_config_t wifi_config = {0};
            wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
            wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
            wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
            if (s_pending_ssid[0] != '\0')
            {
                strncpy((char *)wifi_config.sta.ssid, s_pending_ssid, sizeof(wifi_config.sta.ssid) - 1);
                strncpy((char *)wifi_config.sta.password, s_pending_pass, sizeof(wifi_config.sta.password) - 1);
                esp_wifi_disconnect();
                vTaskDelay(pdMS_TO_TICKS(200));
                esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
            }
            s_retry_num = 0;
            s_allow_sta_connect = true;
            esp_wifi_connect();
        }
        else
        {
            slow_count = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(WIFI_SLOW_RETRY_INTERVAL_MS));
    }
}


static void prv_wifi_connect(const char *ssid, const char *pass)
{
    wifi_config_t wifi_config = {0};

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    if(ssid != NULL && ssid[0] != '\0')
    {
        strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy(s_pending_ssid, ssid, sizeof(s_pending_ssid) - 1);
        s_pending_ssid[sizeof(s_pending_ssid) - 1] = '\0';
    }
    if(pass != NULL)
    {
        strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
        strncpy(s_pending_pass, pass, sizeof(s_pending_pass) - 1);
        s_pending_pass[sizeof(s_pending_pass) - 1] = '\0';
    }

    s_allow_sta_connect = false; // Disable auto-reconnect during config
    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_LOGI(WIFI_CFG_TAG, "Connecting to SSID: %s (Full Channel Scan)", (char *)wifi_config.sta.ssid);
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

/**
 * @brief Khởi tạo mDNS với hostname duy nhất dạng mebieco-xxxxxx.local
 */
static void prv_mdns_init(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(WIFI_CFG_TAG, "mDNS Init failed: %s", esp_err_to_name(err));
        return;
    }

    uint8_t mac[6] = {0};
    char hostname[32] = "mebieco";
    
    // Đọc MAC address từ interface STA để đồng bộ với mạng Wifi kết nối
    err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err == ESP_OK)
    {
        snprintf(hostname, sizeof(hostname), "mebieco-%02x%02x%02x", mac[3], mac[4], mac[5]);
    }
    else
    {
        ESP_LOGW(WIFI_CFG_TAG, "Failed to read STA MAC for mDNS hostname, using default");
    }

    err = mdns_hostname_set(hostname);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_CFG_TAG, "Failed to set mDNS hostname: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(WIFI_CFG_TAG, "mDNS Hostname set/updated: %s.local", hostname);
    }

    err = mdns_instance_name_set("Mebico pH/DO Sensor");
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_CFG_TAG, "Failed to set mDNS instance name: %s", esp_err_to_name(err));
    }

    // Đăng ký dịch vụ HTTP cổng 80 (xóa cũ nếu có để làm mới quảng bá trên các interface vừa active)
    if (mdns_service_exists("_http", "_tcp", NULL))
    {
        mdns_service_remove("_http", "_tcp");
    }
    err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_CFG_TAG, "Failed to add HTTP service to mDNS: %s", esp_err_to_name(err));
    }
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
        wifi_event_sta_disconnected_t *dis_evt = (wifi_event_sta_disconnected_t *)event_data;
        uint8_t reason = dis_evt ? dis_evt->reason : 0;
        ESP_LOGW(WIFI_CFG_TAG, "WiFi STA Disconnected! Reason code: %d", reason);

        if(s_allow_sta_connect)
        {
            // Nếu phát hiện sai/đổi mật khẩu (WIFI_REASON_AUTH_FAIL = 4 hoặc WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT = 15)
            // thì lập tức kích hoạt fallback scan chứ không cố kết nối lại mật khẩu bị lỗi nữa!
            if (reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) {
                ESP_LOGW(WIFI_CFG_TAG, "Auth failure/Timeout detected for SSID: %s. Forcing fallback scan...", s_pending_ssid);
                s_retry_num = EXAMPLE_ESP_MAXIMUM_RETRY;
            }

            if(s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
            {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(WIFI_CFG_TAG, "Fast retry %d/%d for SSID: %s", s_retry_num, EXAMPLE_ESP_MAXIMUM_RETRY, s_pending_ssid);
            }
            else
            {
                if(s_slow_retry_task == NULL)
                {
                    xTaskCreatePinnedToCore(prv_slow_retry_task, "wifi_slow_retry", 5120, NULL, 3, &s_slow_retry_task, 0);
                }

                ESP_LOGI(WIFI_CFG_TAG, "Switch to multi-AP fallback scan every %d s", WIFI_SLOW_RETRY_INTERVAL_MS/1000);
            }
        }
    }
    else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        s_retry_num = 0;
        Sys_Info.isWifiConnected = true;
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(WIFI_CFG_TAG, "Got IP: " IPSTR " for SSID: %s", IP2STR(&event->ip_info.ip), s_pending_ssid);

        prv_align_ap_channel_with_sta();
        prv_mdns_init(); // Gọi lại để cập nhật/phát sóng tên miền trên interface STA khi có IP mới
    }
    else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE)
    {
        uint16_t number = MAX_SCAN_APS;
        memset(s_scan_records, 0, sizeof(s_scan_records));
        esp_err_t err = esp_wifi_scan_get_ap_records(&number, s_scan_records);
        if (err == ESP_OK) {
            s_scan_count = number;
            s_scan_state = WIFI_SCAN_STATE_DONE;
            ESP_LOGI(WIFI_CFG_TAG, "WiFi Scan done! Found %d APs", s_scan_count);
        } else {
            s_scan_count = 0;
            s_scan_state = WIFI_SCAN_STATE_FAILED;
            ESP_LOGE(WIFI_CFG_TAG, "WiFi Scan failed to get records: %s", esp_err_to_name(err));
        }
        wifi_config_manager_finish_scan();
    }
}

static void prv_scan_task(void *pvParameters)
{
    (void)pvParameters;
    s_scan_state = WIFI_SCAN_STATE_SCANNING;
    wifi_config_manager_prepare_scan();

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, false); // non-blocking
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_CFG_TAG, "esp_wifi_scan_start failed: %s", esp_err_to_name(err));
        s_scan_state = WIFI_SCAN_STATE_FAILED;
        wifi_config_manager_finish_scan();
    }
    s_scan_task_handle = NULL;
    vTaskDelete(NULL);
}

void wifi_config_manager_trigger_scan(void)
{
    if (s_scan_state == WIFI_SCAN_STATE_SCANNING) return;
    s_scan_state = WIFI_SCAN_STATE_SCANNING;
    s_scan_count = 0;
    if (s_scan_task_handle == NULL) {
        xTaskCreatePinnedToCore(prv_scan_task, "wifi_scan_task", 3584, NULL, 3, &s_scan_task_handle, 0);
    }
}

wifi_scan_state_t wifi_config_manager_get_scan_state(void)
{
    return s_scan_state;
}

uint16_t wifi_config_manager_get_scan_results(wifi_ap_record_t *out_records, uint16_t max_records)
{
    if (out_records == NULL || max_records == 0) return 0;
    uint16_t copy_cnt = (s_scan_count < max_records) ? s_scan_count : max_records;
    memcpy(out_records, s_scan_records, copy_cnt * sizeof(wifi_ap_record_t));
    return copy_cnt;
}


bool wifi_config_manager_load(char *ssid_out, size_t ssid_len, char *pass_out, size_t pass_len)
{
    saved_wifi_list_t list = {0};
    if (!prv_load_wifi_list(&list) || list.count == 0) return false;

    if (ssid_out != NULL && ssid_len > 0) {
        strncpy(ssid_out, list.items[0].ssid, ssid_len - 1);
        ssid_out[ssid_len - 1] = '\0';
    }
    if (pass_out != NULL && pass_len > 0) {
        strncpy(pass_out, list.items[0].pass, pass_len - 1);
        pass_out[pass_len - 1] = '\0';
    }
    return true;
}

bool wifi_config_manager_save(const char *ssid, const char *pass)
{
    if(ssid == NULL || ssid[0] == '\0') {
        return false;
    }

    saved_wifi_list_t list = {0};
    prv_load_wifi_list(&list);

    int existing_idx = -1;
    for (uint8_t i = 0; i < list.count; i++) {
        if (strcmp(list.items[i].ssid, ssid) == 0) {
            existing_idx = i;
            break;
        }
    }

    saved_wifi_item_t new_item = {0};
    strncpy(new_item.ssid, ssid, sizeof(new_item.ssid) - 1);
    if (pass != NULL) {
        strncpy(new_item.pass, pass, sizeof(new_item.pass) - 1);
    }

    if (existing_idx >= 0) {
        // Cập nhật mật khẩu mới và đưa lên đầu danh sách MRU
        for (int i = existing_idx; i > 0; i--) {
            list.items[i] = list.items[i - 1];
        }
        list.items[0] = new_item;
    } else {
        // Mạng Wi-Fi mới: Đẩy các mạng cũ xuống, chèn mạng mới vào vị trí 0
        uint8_t shift_cnt = (list.count < MAX_SAVED_WIFI) ? list.count : (MAX_SAVED_WIFI - 1);
        for (int i = shift_cnt; i > 0; i--) {
            list.items[i] = list.items[i - 1];
        }
        list.items[0] = new_item;
        if (list.count < MAX_SAVED_WIFI) {
            list.count++;
        }
    }

    if (!prv_save_wifi_list(&list)) return false;

    strncpy(s_pending_ssid, ssid, sizeof(s_pending_ssid) - 1);
    if (pass != NULL) {
        strncpy(s_pending_pass, pass, sizeof(s_pending_pass) - 1);
    }
    return true;
}

bool wifi_config_manager_clear(void)
{
    return wifi_config_manager_clear_all();
}

bool wifi_config_manager_get_list(saved_wifi_list_t *out_list)
{
    return prv_load_wifi_list(out_list);
}

bool wifi_config_manager_remove_entry(const char *ssid)
{
    if (ssid == NULL || ssid[0] == '\0') return false;
    saved_wifi_list_t list = {0};
    if (!prv_load_wifi_list(&list)) return false;

    int found_idx = -1;
    for (uint8_t i = 0; i < list.count; i++) {
        if (strcmp(list.items[i].ssid, ssid) == 0) {
            found_idx = i;
            break;
        }
    }
    if (found_idx < 0) return false;

    for (uint8_t i = found_idx; i < list.count - 1; i++) {
        list.items[i] = list.items[i + 1];
    }
    list.count--;
    memset(&list.items[list.count], 0, sizeof(saved_wifi_item_t));

    return prv_save_wifi_list(&list);
}

bool wifi_config_manager_clear_all(void)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("sys_cfg", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return false;

    nvs_erase_key(my_handle, "wifi_list");
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
    if (!Sys_Info.isWifiConnected)
    {
        ESP_LOGI(WIFI_CFG_TAG, "Pausing auto-connect for WiFi scan...");
        s_was_allow_sta_connect = s_allow_sta_connect;
        s_allow_sta_connect = false;
        esp_wifi_disconnect();
        // Wait a short moment (500ms) for the Wi-Fi driver to fully stop connection attempts
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void wifi_config_manager_finish_scan(void)
{
    if (!Sys_Info.isWifiConnected && s_was_allow_sta_connect)
    {
        ESP_LOGI(WIFI_CFG_TAG, "Resuming auto-connect after WiFi scan...");
        s_allow_sta_connect = true;
        s_retry_num = 0;
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_wifi_connect();
    }
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

    if(has_saved)
    {
        strncpy(s_pending_ssid, ssid, sizeof(s_pending_ssid) - 1);
        s_pending_ssid[sizeof(s_pending_ssid) - 1] = '\0';
        strncpy(s_pending_pass, pass, sizeof(s_pending_pass) - 1);
        s_pending_pass[sizeof(s_pending_pass) - 1] = '\0';
    }

    s_allow_sta_connect = has_saved ? true : false;

#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
#else
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
#endif
    ESP_ERROR_CHECK(esp_wifi_start());

    // Tắt Modem Power Save để Wi-Fi modem luôn chạy 100% công suất không bị lỡ gói rekeying
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

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
        prv_wifi_connect(ssid, pass);
        ESP_LOGI(WIFI_CFG_TAG, "Saved WiFi found, AP+STA enabled");
    }
    else
    {
        ESP_LOGI(WIFI_CFG_TAG, "No saved WiFi, AP only");
    }

    ESP_LOGI(WIFI_CFG_TAG, "AP started SSID:%s", s_ap_ssid);
    prv_mdns_init();
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
