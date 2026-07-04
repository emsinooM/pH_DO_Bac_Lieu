#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "user_azure.h"
#include "esp_http_server.h"



#define WIFI_AP_SSID_PREFIX    "MEBICO_ESP32_PH_"
#define WIFI_AP_PASS    "Mebico@69696969"
#define WIFI_AP_CHANNEL 1
#define WIFI_AP_MAX_CONN 4



bool wifi_config_manager_init(void);
bool wifi_config_manager_load(char *ssid_out, size_t ssid_len, char *pass_out, size_t pass_len);
bool wifi_config_manager_save(const char *ssid, const char *pass);
bool wifi_config_manager_clear(void);
bool azure_config_manager_load(char *host, size_t host_len, char *dev, size_t dev_len, char *sym, size_t sym_len);
bool azure_config_manager_save(const char *host, const char *dev, const char *sym);
bool azure_config_manager_clear(void);
void wifi_config_manager_schedule_connect(void);
void wifi_config_manager_prepare_scan(void);

/* --- Shared Secret Authentication --- */
#define AUTH_SECRET_DEFAULT  "SecretKey"
#define AUTH_SECRET_MAX_LEN  64
bool auth_secret_load(char *out, size_t out_len);
bool auth_secret_save(const char *secret);
bool auth_secret_verify(const char *token);

/* --- WiFi change with automatic fallback --- */
bool wifi_config_manager_set_with_fallback(const char *new_ssid,
                                           const char *new_pass,
                                           uint32_t timeout_ms);
bool is_authenticated(httpd_req_t *req);

