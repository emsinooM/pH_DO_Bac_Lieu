#include "user_azure.h"
#include "esp_ota_ops.h"
#include "azure_sample_connection.h"

/* Azure Provisioning/IoT Hub library includes */
#include "azure_iot_hub_client.h"
#include "azure_iot_hub_client_properties.h"
#include "azure_iot_provisioning_client.h"

/* Azure JSON includes */
#include "azure_iot_json_reader.h"
#include "azure_iot_json_writer.h"

/* Exponential backoff retry include. */
#include "backoff_algorithm.h"

/* Transport interface implementation include header for TLS. */
#include "transport_tls_socket.h"
#include "transport_socket.h"

/* Crypto helper header. */
#include "azure_sample_crypto.h"

/* Demo Specific configs. */
#include "demo_config.h"

/* Demo Specific Interface Functions. */
#include "azure_sample_connection.h"

/* Data Interface Definition */
// #include "sample_azure_iot_pnp_data_if.h"

#include "user_system.h"
#include "user_azure.h"
#include "user_storage.h"
#include "user_system.h"
#include "user_ouput.h"
#include "user_ota.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"     
#include "esp_timer.h"  



// #include "cJSON.h"
#include "queue.h"
#include "FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ph_temp.h" 
#include "filter.h"
#include "do_sensor.h"
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <user_fram.h>
#include "wifi_config_manager.h"
#include "nvs.h"


#undef AZLogInfo
#define AZLogInfo(...)

#define AZURE_WATCHDOG_TIMEOUT_MS  (5 * 60 * 1000)

/* Declare task handle */
TaskHandle_t Azure_Process_Handle;
TaskHandle_t Azure_Transmit_Handle;
TaskHandle_t Azure_Telemetry_Handle;
TaskHandle_t Azure_Offline_Sync_Handle;


/* Declare IoT hub handle */
IoTHubHandle_t IoTHubHandle;

/* Declare mutex for receive and transmit proceess */
SemaphoreHandle_t azureMutex;

/* Declare queue for command and telemetry data */
QueueHandle_t xQueueTelemetry;
QueueHandle_t xQueueResponse;

/* === On-demand telemetry control === */
static volatile bool g_telemetry_active = false;
static volatile uint32_t g_telemetry_interval_ms = 5000;
bool bIsOtaActivated = false;
char g_last_telemetry_payload[512] = {0};


bool User_Azure_Get_Telemetry_Active(void) {
    return g_telemetry_active;
}

uint32_t User_Azure_Get_Telemetry_Interval(void) {
    return g_telemetry_interval_ms;
}

/* Task Function Prototype */
static void Azure_Process_Loop_Task(void *pvParameters);
static void Azure_Transmit_Task(void *pvParameters);
static void prvFormatScheduleTime(time_t ts, char *buf, size_t buf_len);
static void Azure_Telemetry_Task(void *pvParameters);
static void Azure_Offline_Sync_Task(void *pvParameters);

/* === WiFi config change via Direct Method === */
typedef struct {
    char ssid[32];
    char pass[64];
} wifi_change_request_t;

static void prv_wifi_change_task(void *pvParameters);
/* Ensure NetworkContext is a complete type for stack allocation. The
 * middleware headers typedef an opaque struct; provide a minimal
 * definition used by the sample demos. */
struct NetworkContext
{
    void *pParams;
};

/* Some demo symbols are provided by the sample application. Provide
 * minimal fallbacks here when they are not supplied by the project so
 * the file builds. These are guarded so a real sample implementation
 * will take precedence if present. */
#undef democonfigNETWORK_BUFFER_SIZE
#define democonfigNETWORK_BUFFER_SIZE 2048U


AzureIoTHubClient_t xAzureIoTHubClient;

static uint8_t ucMQTTMessageBuffer[democonfigNETWORK_BUFFER_SIZE];
// static uint8_t ucScratchBuffer[ 512 ];
// static uint8_t ucReportedPropertiesUpdate[ 380 ];
// static uint32_t ulReportedPropertiesUpdateLength = 0U;

uint32_t ulScratchBufferLength = 0U;
NetworkCredentials_t xNetworkCredentials = {0};
AzureIoTTransportInterface_t xTransport;
NetworkContext_t xNetworkContext = {0};
TlsTransportParams_t xTlsTransportParams = {0};
AzureIoTResult_t xResult;
uint32_t ulStatus;
AzureIoTHubClientOptions_t xHubOptions = {0};
bool xSessionPresent;

uint8_t *pucIotHubHostname = (uint8_t *)IoTHubHandle.hostName;
uint8_t *pucIotHubDeviceId = (uint8_t *)IoTHubHandle.deviceId;

/* Simple unix time provider fallback. If your project provides a
 * higher-precision implementation, it will be used instead. */
uint64_t ullGetUnixTime(void)
{
    time_t now = time(NULL);

    /* If system time is set (non-zero and reasonable), return epoch seconds.
     * Otherwise fall back to tick-based uptime (best-effort). */
    if (now > (time_t)1600000000)
    {
        return (uint64_t)now;
    }

    return (uint64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000ULL);
}

/* Minimal stubs for platform/sample helper functions. Real
 * implementations should be provided by the project (these return
 * success so demos can compile). */
static uint32_t prvSetupNetworkCredentials(NetworkCredentials_t *pxNetworkCredentials)
{
    pxNetworkCredentials->xDisableSni = pdFALSE;

    /* Set the credentials for establishing a TLS connection. */
    pxNetworkCredentials->pucRootCa = (const unsigned char *)democonfigROOT_CA_PEM;
    pxNetworkCredentials->xRootCaSize = sizeof(democonfigROOT_CA_PEM);

#ifdef democonfigCLIENT_CERTIFICATE_PEM
    pxNetworkCredentials->pucClientCert = (const unsigned char *)democonfigCLIENT_CERTIFICATE_PEM;
    pxNetworkCredentials->xClientCertSize = sizeof(democonfigCLIENT_CERTIFICATE_PEM);
    pxNetworkCredentials->pucPrivateKey = (const unsigned char *)democonfigCLIENT_PRIVATE_KEY_PEM;
    pxNetworkCredentials->xPrivateKeySize = sizeof(democonfigCLIENT_PRIVATE_KEY_PEM);
#endif

    return 0U;
}
static uint32_t prvConnectToServerWithBackoffRetries(const char *pcHostName,
                                                     uint32_t ulPort,
                                                     NetworkCredentials_t *pxNetworkCredentials,
                                                     NetworkContext_t *pxNetworkContext)
{
    if ((pcHostName == NULL) || (pxNetworkCredentials == NULL) || (pxNetworkContext == NULL))
    {
        return 1U;
    }

    /* Use the TLS socket connect implementation to initialize transport
     * internals (this will allocate and set pxTlsParams->xSSLContext). */
    TlsTransportStatus_t xTlsStatus = TLS_Socket_Connect(pxNetworkContext,
                                                         pcHostName,
                                                         (uint16_t)ulPort,
                                                         pxNetworkCredentials,
                                                         2000U,
                                                         2000U);

    return (xTlsStatus == eTLSTransportSuccess) ? 0U : 1U;
}

static void prvHandleCloudMessageTest(AzureIoTHubClientCloudToDeviceMessageRequest_t *pxMessage,
                                      void *pvContext)
{
    ESP_LOGI("AZURE:", "Receive C2D message: %.*s", (int)pxMessage->ulPayloadLength, (const char *)pxMessage->pvMessagePayload);
}

/* Kiểm tra method name chính xác: So sánh cả nội dung lẫn độ dài */
static bool prvMethodNameMatch(AzureIoTHubClientCommandRequest_t *pxMessage, const char *expected){
    size_t expected_len = strlen(expected);
    return (pxMessage->usCommandNameLength == (uint16_t)expected_len) && (strncmp((const char*) pxMessage->pucCommandName, expected, expected_len) == 0);
}

static void prvHandleCommand(AzureIoTHubClientCommandRequest_t *pxMessage,
                             void *pvContext)
{
    uint16_t _code = 99;
    cJSON *res = cJSON_CreateObject();
    DirectMethodResponse_t response;
    memset(&response, 0, sizeof(response));

    ESP_LOGI("AZURE", "\n\n");
    ESP_LOGW("AZURE: ", "---------- Direct method ----------");
    ESP_LOGI("AZ: Direct method", "%.*s\nCommand name: %.*s", (int)pxMessage->ulPayloadLength, (char *)pxMessage->pvMessagePayload, (int)pxMessage->usCommandNameLength, (char *)pxMessage->pucCommandName);

    if (   prvMethodNameMatch(pxMessage, "Update")
        || prvMethodNameMatch(pxMessage, "CheckVer")
        || prvMethodNameMatch(pxMessage, "GetPH")
        || prvMethodNameMatch(pxMessage, "CalibratePH7")
        || prvMethodNameMatch(pxMessage, "CalibratePH4")
        || prvMethodNameMatch(pxMessage, "CalibratePH686")
        || prvMethodNameMatch(pxMessage, "CalibratePH918")
        || prvMethodNameMatch(pxMessage, "CalibratePH10")
        || prvMethodNameMatch(pxMessage, "CalibratePH")
        || prvMethodNameMatch(pxMessage, "SetDeviceConfig")
        || prvMethodNameMatch(pxMessage, "CalibrateDO")
        || prvMethodNameMatch(pxMessage, "Reboot")
        || prvMethodNameMatch(pxMessage, "GetFullTelemetry")
        || prvMethodNameMatch(pxMessage, "GetTelemetry")
    )
    {
        ESP_LOGI("AZURE: ", "Received direct method");

        char *buf = calloc(pxMessage->ulPayloadLength + 1, sizeof(char));
        if (buf != NULL)
        {
            memcpy(buf, pxMessage->pvMessagePayload, pxMessage->ulPayloadLength);
            buf[pxMessage->ulPayloadLength] = '\0';

            cJSON *parsed = cJSON_Parse(buf);
            cJSON *code = cJSON_GetObjectItem(parsed, "Code");
            if (code)
            {
                _code = code->valueint;
            }

            if (parsed)
            {
                // Azure_Handle_Direct_Method_Data(parsed, &response);
                // cJSON_Delete(parsed);
                cJSON *code_item = cJSON_GetObjectItem(parsed, "Code");
                uint16_t payload_code = (code_item && cJSON_IsNumber(code_item)) ? (uint16_t)code_item->valueint : 0;
                
                // Bảng mapping: method name phải khớp với code trong payload
                bool code_method_mismatch = false;
                const char *method_name = (const char *)pxMessage->pucCommandName;

                if(prvMethodNameMatch(pxMessage, "Update") && payload_code != CMD_CODE_UPDATE_FIRMWARE) 
                    code_method_mismatch = true;
                else if(prvMethodNameMatch(pxMessage, "CheckVer") && payload_code != CMD_CODE_ASK_VERSION){
                    code_method_mismatch = true;
                }
                else if (prvMethodNameMatch(pxMessage, "GetPH") && payload_code != CMD_CODE_GET_PH)
                    code_method_mismatch = true;
                else if (prvMethodNameMatch(pxMessage, "CalibratePH7") && payload_code != CMD_CODE_PH_CALIBRATE)
                    code_method_mismatch = true;
                else if (prvMethodNameMatch(pxMessage, "CalibratePH4") && payload_code != CMD_CODE_PH_CALIBRATE)
                    code_method_mismatch = true;
                else if (prvMethodNameMatch(pxMessage, "CalibratePH686") && payload_code != CMD_CODE_PH_CALIBRATE)
                    code_method_mismatch = true;
                else if (prvMethodNameMatch(pxMessage, "CalibratePH918") && payload_code != CMD_CODE_PH_CALIBRATE)
                    code_method_mismatch = true;
                else if (prvMethodNameMatch(pxMessage, "CalibratePH10") && payload_code != CMD_CODE_PH_CALIBRATE)
                    code_method_mismatch = true;
                else if (prvMethodNameMatch(pxMessage, "CalibratePH") && payload_code != CMD_CODE_PH_CALIBRATE)
                    code_method_mismatch = true;
                else if (prvMethodNameMatch(pxMessage, "CalibrateDO") && payload_code != CMD_CODE_DO_CALIBRATE)
                    code_method_mismatch = true;
                else if (prvMethodNameMatch(pxMessage, "SetDeviceConfig") && payload_code != CMD_CODE_SET_DEVICE_CONFIG)
                    code_method_mismatch = true;
                else if (prvMethodNameMatch(pxMessage, "Reboot") && payload_code != CMD_CODE_REBOOT)
                    code_method_mismatch = true;
                else if (prvMethodNameMatch(pxMessage, "GetFullTelemetry") && payload_code != CMD_CODE_GET_FULL_TELEMETRY)
                    code_method_mismatch = true;
                else if (prvMethodNameMatch(pxMessage, "GetTelemetry") && payload_code != CMD_CODE_GET_FULL_TELEMETRY)
                    code_method_mismatch = true;
                
                if (code_method_mismatch){
                    ESP_LOGE("AZURE: CMD CALLBACK", "Method name '%.*s' does not match Code %d", pxMessage->usCommandNameLength, method_name, payload_code);
                    response.status = COMMAND_STATUS_BAD_REQUEST;
                    response.payloadLength = snprintf(response.payload, sizeof(response.payload), "Method '%.*s' not allowed with Code %d", pxMessage->usCommandNameLength, method_name, payload_code);
                }
                else{
                    Azure_Handle_Direct_Method_Data(parsed, &response);
                }
                cJSON_Delete(parsed);
            }
            else
            {
                ESP_LOGE("AZURE: ", "Parse json fail");
            }

            free(buf);
        }
        else
        {
            ESP_LOGE("AZURE: CMD CALLBACK", "Cannot allocate buffer for message");
            response.status = COMMAND_STATUS_DEVICE_ERROR;
            response.payloadLength = sprintf(response.payload, "Payload too long, device not enough ram");
        }
    }
    else
    {
        ESP_LOGE("AZURE: CMD CALLBACK", "Do not have Control method");
        response.status = COMMAND_STATUS_NOT_FOUND;
        response.payloadLength = sprintf(response.payload, "Method name: %.*s do not support", pxMessage->usCommandNameLength, pxMessage->pucCommandName);
    }

    cJSON_AddNumberToObject(res, "Code", _code);
    if (_code == CMD_CODE_ASK_VERSION)
    {
        char formatted_time[32];
        prvFormatScheduleTime(time(NULL), formatted_time, sizeof(formatted_time));
        cJSON_AddStringToObject(res, "Time", formatted_time);
    }
    else
    {
        cJSON_AddNumberToObject(res, "TimeStamp", time(NULL));
    }
    cJSON_AddStringToObject(res, "Message", response.payload);
    // cJSON_AddStringToObject(res, "HostName", IoTHubHandle.hostName);
    // cJSON_AddStringToObject(res, "DeviceID", IoTHubHandle.deviceId);

    /* Thêm object Weight vào response nếu là lệnh đọc cân */
    
    if (_code == CMD_CODE_GET_PH)
    {
        PH_Temp_Sensor_Status_t status = Get_Sensor_Status();
        cJSON *sensor = cJSON_CreateObject();
        if (sensor != NULL)
        {
            cJSON_AddNumberToObject(sensor, "ph", status.ph);
            cJSON_AddNumberToObject(sensor, "temp", status.temperature);
            cJSON_AddNumberToObject(sensor, "v_probe_mv", status.v_probe_mv);
            cJSON_AddBoolToObject(sensor, "is_calibrated", status.is_calibrated);
            cJSON_AddNumberToObject(sensor, "filter_lvl", g_filter_level);
            cJSON_AddNumberToObject(sensor, "ph7_voltage_mv", status.ph7_voltage_mv);
            cJSON_AddNumberToObject(sensor, "ph7_temp_c", status.ph7_temp_c);
            cJSON_AddNumberToObject(sensor, "ph4_voltage_mv", status.ph4_voltage_mv);
            cJSON_AddNumberToObject(sensor, "ph4_temp_c", status.ph4_temp_c);
            cJSON_AddNumberToObject(sensor, "slope_norm", status.slope_norm);
            cJSON_AddNumberToObject(sensor, "u7", status.u7);
            cJSON_AddItemToObject(res, "sensor", sensor);
        }
        /* Thêm trạng thái telemetry vào response */
        cJSON_AddBoolToObject(res, "TelemetryActive", g_telemetry_active);
        cJSON_AddNumberToObject(res, "IntervalMs",
            g_telemetry_active ? (double)g_telemetry_interval_ms : 0);
    }
    else if (_code == CMD_CODE_GET_FULL_TELEMETRY)
    {
        PH_Temp_Sensor_Status_t status = Get_Sensor_Status();

        int16_t rssi = -120;
        char wifi_ssid[32] = {0};
        char wifi_pass[64] = {0};
        wifi_config_manager_load(wifi_ssid, sizeof(wifi_ssid), wifi_pass, sizeof(wifi_pass));
        if (Is_System_Internet_Connected())
        {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
            {
                rssi = ap_info.rssi;
                if (wifi_ssid[0] == '\0')
                {
                    strncpy(wifi_ssid, (const char *)ap_info.ssid, sizeof(wifi_ssid) - 1);
                }
            }
        }

        uint32_t uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000ULL);

        esp_reset_reason_t reason = esp_reset_reason();
        const char *reason_str = "Unknown";
        switch (reason) {
            case ESP_RST_POWERON:   reason_str = "Power-on Reset"; break;
            case ESP_RST_EXT:       reason_str = "External Reset"; break;
            case ESP_RST_SW:        reason_str = "Software Reset"; break;
            case ESP_RST_PANIC:     reason_str = "Software Panic"; break;
            case ESP_RST_INT_WDT:   reason_str = "Interrupt Watchdog"; break;
            case ESP_RST_TASK_WDT:  reason_str = "Task Watchdog"; break;
            case ESP_RST_DEEPSLEEP: reason_str = "Deepsleep Reset"; break;
            case ESP_RST_BROWNOUT:  reason_str = "Brownout Reset"; break;
            default: break;
        }

        uint32_t free_ram = esp_get_free_heap_size();
        const esp_partition_t *running_part = esp_ota_get_running_partition();
        const char *part_label = (running_part != NULL) ? running_part->label : "Unknown";

        cJSON *pl = cJSON_CreateObject();
        cJSON *sensor_data = cJSON_CreateObject();
        if (pl != NULL && sensor_data != NULL)
        {
            cJSON_AddStringToObject(pl, "HostName", IoTHubHandle.hostName);
            cJSON_AddStringToObject(pl, "DeviceId", IoTHubHandle.deviceId);
            cJSON_AddNumberToObject(pl, "Code", 508);
            cJSON_AddNumberToObject(pl, "TimeStamp", (double)time(NULL));

            cJSON_AddNumberToObject(sensor_data, "ph", status.ph);
            cJSON_AddNumberToObject(sensor_data, "ph_temp", status.temperature);
            cJSON_AddBoolToObject(sensor_data, "Valid", status.is_calibrated);
            cJSON_AddNumberToObject(sensor_data, "do", status.do_mg_l);
            cJSON_AddNumberToObject(sensor_data, "temp", status.do_temp_c);
            cJSON_AddNumberToObject(sensor_data, "do_sat", status.do_saturation_pct);
            cJSON_AddBoolToObject(sensor_data, "do_valid", status.do_valid);
            cJSON_AddNumberToObject(sensor_data, "rssi", rssi);
            cJSON_AddStringToObject(sensor_data, "wifi_ssid", wifi_ssid);
            cJSON_AddStringToObject(sensor_data, "wifi_pass", wifi_pass);
            cJSON_AddNumberToObject(sensor_data, "uptime", uptime_sec);
            cJSON_AddStringToObject(sensor_data, "reset_reason", reason_str);
            cJSON_AddNumberToObject(sensor_data, "free_ram", free_ram);
            cJSON_AddNumberToObject(sensor_data, "v_probe_mv", status.v_probe_mv);
            cJSON_AddNumberToObject(sensor_data, "do_err", status.do_error_code);
            cJSON_AddStringToObject(sensor_data, "ver", VERSION);
            cJSON_AddStringToObject(sensor_data, "ota_part", part_label);

            cJSON *ph_calib = cJSON_CreateObject();
            if (ph_calib != NULL)
            {
                cJSON_AddBoolToObject(ph_calib, "is_calibrated", ph_cal.is_calibrated);
                cJSON_AddNumberToObject(ph_calib, "cal_type", ph_cal.cal_type);
                cJSON_AddNumberToObject(ph_calib, "ph7_mv", ph_cal.ph7_voltage_mv);
                cJSON_AddNumberToObject(ph_calib, "ph7_temp", ph_cal.ph7_temp_c);
                cJSON_AddNumberToObject(ph_calib, "ph4_mv", ph_cal.ph4_voltage_mv);
                cJSON_AddNumberToObject(ph_calib, "ph4_temp", ph_cal.ph4_temp_c);
                cJSON_AddNumberToObject(ph_calib, "ph10_mv", ph_cal.ph10_voltage_mv);
                cJSON_AddNumberToObject(ph_calib, "ph10_temp", ph_cal.ph10_temp_c);
                cJSON_AddNumberToObject(ph_calib, "slope_norm", ph_cal.slope_norm);
                cJSON_AddNumberToObject(ph_calib, "slope_high", ph_cal.slope_high);
                cJSON_AddNumberToObject(ph_calib, "u7", ph_cal.u7);
                cJSON_AddItemToObject(sensor_data, "ph_calib", ph_calib);
            }

            cJSON_AddItemToObject(pl, "SensorData", sensor_data);
            cJSON_AddItemToObject(res, "payload", pl);
        }
    }
    char *jsonStr = cJSON_PrintUnformatted(res);

    ESP_LOGI("AZURE: ", "Sending response to Cloud: %s", jsonStr);

    AzureIoTHubClient_SendCommandResponse(&xAzureIoTHubClient, pxMessage, response.status, (const uint8_t *)jsonStr, strlen(jsonStr));

    cJSON_Delete(res);
    free(jsonStr);

    ESP_LOGI(" ", "\n\n");
}

static void prvFormatScheduleTime(time_t ts, char *buf, size_t buf_len)
{
    struct tm tm_time = {0};

    if(buf == NULL || buf_len == 0)
    {
        return;
    }

    if(ts <= 0 || localtime_r(&ts, &tm_time) == NULL)
    {
        snprintf(buf, buf_len, "N/A");
        return;
    }

    strftime(buf, buf_len, "%d/%m/%Y %H:%M:%S", &tm_time);
}

/* Provide the sample connection check used by the Azure demos. Forward
 * to the project's network status function so existing system code is
 * reused. */
bool xAzureSample_IsConnectedToInternet(void)
{
    return Is_System_Internet_Connected();
}

void User_Azure_Connect(void)
{
    // lệnh in ra Terminal thông tin đang cấu hình:
    ESP_LOGW("AZURE: CONFIG", "Đang thử kết nối... Host: %s", IoTHubHandle.hostName);
    ESP_LOGW("AZURE: CONFIG", "Device ID: %s", IoTHubHandle.deviceId);
    ESP_LOGW("AZURE: CONFIG", "Key: %s", IoTHubHandle.symmetricKey);

    uint32_t pulIothubHostnameLength = strlen(IoTHubHandle.hostName);
    uint32_t pulIothubDeviceIdLength = strlen(IoTHubHandle.deviceId);

    /* Initialize Azure IoT Middleware.  */
    static bool isAzureIotSysInited = false;
    if (!isAzureIotSysInited)
    {
        configASSERT(AzureIoT_Init() == eAzureIoTSuccess);
        isAzureIotSysInited = true;
    }

    ulStatus = prvSetupNetworkCredentials(&xNetworkCredentials);
    configASSERT(ulStatus == 0);

    xNetworkContext.pParams = &xTlsTransportParams;

    if (xAzureSample_IsConnectedToInternet())
    {
        ulStatus = prvConnectToServerWithBackoffRetries((const char *)pucIotHubHostname,
                                                        democonfigIOTHUB_PORT,
                                                        &xNetworkCredentials, &xNetworkContext);
        // configASSERT( ulStatus == 0 );
        if (ulStatus != 0)
        {
            ESP_LOGE("AZURE", "Connect to TLS server failed. Retrying in 5 seconds...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            return;
        }

        /* Fill in Transport Interface send and receive function pointers. */
        xTransport.pxNetworkContext = &xNetworkContext;
        xTransport.xSend = TLS_Socket_Send;
        xTransport.xRecv = TLS_Socket_Recv;

        /* Init IoT Hub option */
        xResult = AzureIoTHubClient_OptionsInit(&xHubOptions);
        configASSERT(xResult == eAzureIoTSuccess);

        xHubOptions.pucModuleID = (const uint8_t *)democonfigMODULE_ID;
        xHubOptions.ulModuleIDLength = sizeof(democonfigMODULE_ID) - 1;
        xHubOptions.pucModelID = (const uint8_t *)sampleazureiotMODEL_ID;
        xHubOptions.ulModelIDLength = sizeof(sampleazureiotMODEL_ID) - 1;

        xResult = AzureIoTHubClient_Init(&xAzureIoTHubClient,
                                         pucIotHubHostname, pulIothubHostnameLength,
                                         pucIotHubDeviceId, pulIothubDeviceIdLength,
                                         &xHubOptions,
                                         ucMQTTMessageBuffer, sizeof(ucMQTTMessageBuffer),
                                         ullGetUnixTime,
                                         &xTransport);
        configASSERT(xResult == eAzureIoTSuccess);

#ifdef democonfigDEVICE_SYMMETRIC_KEY
        xResult = AzureIoTHubClient_SetSymmetricKey(&xAzureIoTHubClient,
                                                    (const uint8_t *)IoTHubHandle.symmetricKey,
                                                    strlen((const char *)IoTHubHandle.symmetricKey),
                                                    Crypto_HMAC);
        if (xResult != eAzureIoTSuccess)
        {
            ESP_LOGE("AZURE", "Set Symmetric Key failed: %d (check Azure Key format). Disconnecting...", xResult);
            TLS_Socket_Disconnect(&xNetworkContext);
            vTaskDelay(pdMS_TO_TICKS(5000));
            return;
        }
#endif /* democonfigDEVICE_SYMMETRIC_KEY */

        /* Sends an MQTT Connect packet over the already established TLS connection,
         * and waits for connection acknowledgment (CONNACK) packet. */
        LogInfo(("Creating an MQTT connection to %s.\r\n", pucIotHubHostname));

        xResult = AzureIoTHubClient_Connect(&xAzureIoTHubClient,
                                            false, &xSessionPresent,
                                            5000U);
        if (xResult != eAzureIoTSuccess)
        {
            ESP_LOGE("AZURE", "MQTT Connect to IoT Hub failed: %d. Disconnecting and retrying in 5s...", xResult);
            TLS_Socket_Disconnect(&xNetworkContext);
            vTaskDelay(pdMS_TO_TICKS(5000));
            return;
        }

        xResult = AzureIoTHubClient_SubscribeCloudToDeviceMessage(&xAzureIoTHubClient, prvHandleCloudMessageTest,
                                                                  &xAzureIoTHubClient, 5000U);
        if (xResult != eAzureIoTSuccess)
        {
            ESP_LOGE("AZURE", "Subscribe C2D failed: %d. Disconnecting...", xResult);
            TLS_Socket_Disconnect(&xNetworkContext);
            vTaskDelay(pdMS_TO_TICKS(5000));
            return;
        }

        xResult = AzureIoTHubClient_SubscribeCommand(&xAzureIoTHubClient, prvHandleCommand,
                                                     &xAzureIoTHubClient, 5000U);
        if (xResult != eAzureIoTSuccess)
        {
            ESP_LOGE("AZURE", "Subscribe Command failed: %d. Disconnecting...", xResult);
            TLS_Socket_Disconnect(&xNetworkContext);
            vTaskDelay(pdMS_TO_TICKS(5000));
            return;
        }


        IoTHubHandle.isAzureInitialized = true;

        // Báo cho OTA Bootloader biết FW mới kết nối mượt mà, huỷ bỏ Rollback
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI("AZURE", "App marked as valid, OTA rollback cancelled.");
             

        if (IoTHubHandle.isProcessLoopInitialized == false)
        {
            if (xTaskCreatePinnedToCore(Azure_Process_Loop_Task, "Azure process loop", 2 * 4096, NULL, 5, &Azure_Process_Handle, 0) == pdPASS)
            {
                ESP_LOGI("AZURE: PROCESS LOOP", "Create process loop task suscessfully");
                // IoTHubHandle.isProcessLoopInitialized = true;
            }
            else
            {
                ESP_LOGI("AZURE: PROCESS LOOP", "Create process loop task fail\n");
            }
        }

        if (IoTHubHandle.isTransmitInitialized == false)
        {
            if (xTaskCreatePinnedToCore(Azure_Transmit_Task, "Azure transmit", 3 * 4096, NULL, 2, &Azure_Transmit_Handle, 0) == pdPASS)
            {
                ESP_LOGI("AZURE: TRANSMIT", "Create transmit task suscessfully");
                // IoTHubHandle.isTransmitInitialized = true;
            }
            else
            {
                ESP_LOGI("AZURE: TRANSMIT", "Create transmit task fail\n");
            }
        }

        if (IoTHubHandle.isTelemetryInitialized == false)
        {
            if (xTaskCreatePinnedToCore(Azure_Telemetry_Task, "Azure telemetry", 3 * 4096, NULL, 2, &Azure_Telemetry_Handle, 0) == pdPASS)
            {
                ESP_LOGI("AZURE: TELEMETRY", "Create telemetry task suscessfully");
                IoTHubHandle.isTelemetryInitialized = true;
            }
            else
            {
                ESP_LOGI("AZURE: TELEMETRY", "Create telemetry task fail\n");
            }
        }

        if (IoTHubHandle.isOfflineSyncInitialized == false)
        {
            if (xTaskCreatePinnedToCore(Azure_Offline_Sync_Task, "Azure offline sync", 4 * 4096, NULL, 2, &Azure_Offline_Sync_Handle, 0) == pdPASS)
            {
                ESP_LOGI("AZURE: OFFLINE SYNC", "Create offline sync task successfully");
                IoTHubHandle.isOfflineSyncInitialized = true;
            }
            else
            {
                ESP_LOGI("AZURE: OFFLINE SYNC", "Create offline sync task fail\n");
            }
        }

// --- KIỂM TRA BÁO CÁO KẾT QUẢ OTA LÊN CLOUD ---
     char ota_res[16] = {0};
     if (Nvs_Read_String("ota_res", ota_res) && strlen(ota_res) > 0)
     {
        int wait_timeout = 0;
        while (!IoTHubHandle.isTransmitInitialized && wait_timeout < 20)
        {
            vTaskDelay(pdMS_TO_TICKS(100)); // Chờ 100ms mỗi lần
            wait_timeout++;
        }
         char ota_err[64] = {0};
         Nvs_Read_String("ota_err", ota_err);
         
                  char report_str[384]; // Nới rộng buffer lên 384 bytes cho an toàn
         snprintf(report_str, sizeof(report_str),
             "{\"payload\":{"
                 "\"HostName\":\"%s\","
                 "\"DeviceId\":\"%s\","
                 "\"Code\":501,"
                 "\"TimeStamp\":%lld,"
                 "\"OtaResult\":\"%s\","
                 "\"OtaError\":\"%s\","
                 "\"FirmwareVer\":\"%s\""
             "}}",
             IoTHubHandle.hostName,
             IoTHubHandle.deviceId,
             (long long)time(NULL),
             ota_res,
             ota_err,
             VERSION
         );

         
         ESP_LOGW("AZURE: OTA REPORT", "Sending OTA report to Azure: %s", report_str);
         PushTelemetry(report_str); // Đẩy báo cáo vào hàng đợi gửi đi
         
         // Xóa cờ trong NVS để tránh gửi lặp lại ở những lần khởi động sau
         Nvs_Write_String("ota_res", "");
         Nvs_Write_String("ota_err", "");
     }
    }
}


void User_Azure_Task(void)
{
    /* Create mutex */
    azureMutex = xSemaphoreCreateMutex();

    if (azureMutex == NULL)
    {
        ESP_LOGE("AZURE: CREATE MUTEX", "Cannot create mutex\n\n\n");
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    uint32_t last_azure_ok_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    while (1)
    {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Kiểm tra WDG khi chưa / đang mất kết nối Azure
        if(IoTHubHandle.isAzureInitialized){
            last_azure_ok_time = now;
        }
        else{
            bool has_credentials = (strlen(IoTHubHandle.hostName) > 0 && strlen(IoTHubHandle.deviceId) > 0 && strlen(IoTHubHandle.symmetricKey) > 0);
            if(Is_System_Internet_Connected() && has_credentials && !bIsOtaActivated){
                if(now - last_azure_ok_time > AZURE_WATCHDOG_TIMEOUT_MS){
                    ESP_LOGE("AZURE: WATCHDOG", "Azure mat ket noi qua %lu ms. Dang tu dong reset chip...", (unsigned long)AZURE_WATCHDOG_TIMEOUT_MS);
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    esp_restart();
                }
            }
            else{
                last_azure_ok_time = now;
            }
        }
        
        if (!Is_System_Internet_Connected())
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (strlen(IoTHubHandle.hostName) == 0 || strlen(IoTHubHandle.deviceId) == 0 || strlen(IoTHubHandle.symmetricKey) == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (IoTHubHandle.isNeedReinit)
        {
            if (xSemaphoreTake(azureMutex, pdMS_TO_TICKS(3000U)) == pdTRUE)
            {
                IoTHubHandle.isNeedReinit = false;
                if (IoTHubHandle.isAzureInitialized)
                {
                    AzureIoTHubClient_Deinit(&xAzureIoTHubClient);
                    TLS_Socket_Disconnect(&xNetworkContext);
                    IoTHubHandle.isAzureInitialized = false;
                }
                xSemaphoreGive(azureMutex);
            }
            else
            {
                ESP_LOGE("AZURE: REINIT", "Cannot get mutex to deinit");
            }
        }


        if(bIsOtaActivated)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!IoTHubHandle.isAzureInitialized)
        {
            User_Azure_Connect();
        }

        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
            now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (IoTHubHandle.isAzureInitialized){
                last_azure_ok_time = now;
            }
            else{
                if(Is_System_Internet_Connected() && !bIsOtaActivated){
                    if (now - last_azure_ok_time > AZURE_WATCHDOG_TIMEOUT_MS){
                        ESP_LOGE("AZURE: WATCHDOG", "Azure mat ket noi qua %lu ms. Dang tu dong reset chip...", (unsigned long)AZURE_WATCHDOG_TIMEOUT_MS);
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        esp_restart();
                    }
                }
                else{
                    last_azure_ok_time = now;
                }
            }
            if (IoTHubHandle.isNeedReinit || !Is_System_Internet_Connected())
            {
                break;
            }
        }
    }
}

/* ================================================================
 *  OTA memory cleanup — delete Azure sub-tasks to free their
 *  stack memory (~32 KB) before the HTTPS download begins.
 *  Called from User_Ota_Task() AFTER Azure has been deinited.
 * ================================================================ */
void User_Azure_Cleanup_For_OTA(void)
{
    ESP_LOGI("AZURE", "Reboot-to-OTA model active. Memory cleanup is handled by esp_restart().");
}


static void Azure_Process_Loop_Task(void *pvParameters)
{

    AzureIoTResult_t result;
    IoTHubHandle.isProcessLoopInitialized = true;

    while (1)
    {
        if (!Is_System_Internet_Connected() || !IoTHubHandle.isAzureInitialized || IoTHubHandle.isNeedReinit)
        {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (xSemaphoreTake(azureMutex, pdMS_TO_TICKS(1000U)) == pdTRUE)
        {
            result = AzureIoTHubClient_ProcessLoop(&xAzureIoTHubClient, 100U);
            if (result != eAzureIoTSuccess)
            {
                ESP_LOGE("AZURE: PROCESS LOOP", "Error code: %d", result);
                IoTHubHandle.isNeedReinit = true;
                IoTHubHandle.isAzureInitialized = false;
            }

            xSemaphoreGive(azureMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void Azure_Transmit_Task(void *pvParameters)
{

    TelemetryEvent_t telemetry;
    // ResponseEvent_t response;
    AzureIoTResult_t result;

    xQueueTelemetry = xQueueCreate(TELEMETRY_QUEUE_LENGTH, sizeof(TelemetryEvent_t));
    if (xQueueTelemetry == NULL)
    {

        while (1)
        {
            ESP_LOGE("AZURE: TRANSMIT TASK", "Cannot create telemetry queue, need to check\n\n\n");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
    else
    {
        IoTHubHandle.isTransmitInitialized = true;
    }

    while (1)
    {
        if (xQueueReceive(xQueueTelemetry, &telemetry, 0) == pdTRUE)
        {
            if(bIsOtaActivated ||!Is_System_Internet_Connected() || !IoTHubHandle.isAzureInitialized || IoTHubHandle.isNeedReinit)
            {
                ESP_LOGW("AZURE: TRANSMIT TASK", "Skip telemetry (not connected)");
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }

            if (xSemaphoreTake(azureMutex, pdMS_TO_TICKS(3000U)) == pdTRUE)
            {
                result = AzureIoTHubClient_SendTelemetry(&xAzureIoTHubClient, (const uint8_t *)telemetry.payload, strlen(telemetry.payload), NULL, eAzureIoTHubMessageQoS1, NULL);
                if (result != eAzureIoTSuccess)
                {
                    ESP_LOGE("AZURE: TRANSMIT TASK", "Send telemetry failed: %d", result);
                    IoTHubHandle.isNeedReinit = true;
                    IoTHubHandle.isAzureInitialized = false;
                }
                xSemaphoreGive(azureMutex);
            }
            else
            {
                ESP_LOGE("AZURE: TRANSMIT TASK", "Cannot get mutex");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void prv_telemetry_state_save(bool active, uint32_t interval_ms){
    nvs_handle_t handle;
    esp_err_t err = nvs_open("sys_cfg", NVS_READWRITE, &handle);
    if(err != ESP_OK){
        ESP_LOGE("TELE_NVS", "nvs_open failed: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_u8(handle, "tele_on", active ? 1 : 0);
    nvs_set_u32(handle, "tele_intv", interval_ms);
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI("TELE_NVS", "Saved state: active=%d, interval=%lu ms", active, (unsigned long)interval_ms);
}

static void prv_telemetry_state_load(void){
    nvs_handle_t handle;
    esp_err_t err = nvs_open("sys_cfg", NVS_READONLY, &handle);
    if(err != ESP_OK){
        ESP_LOGE("TELE_NVS", "nvs_open failed (first boot?), using defaults");
        return;
    }

    uint8_t on = 0;
    uint32_t intv = 5000;

    if (nvs_get_u8(handle, "tele_on", &on) == ESP_OK){
        g_telemetry_active = (on != 0);
    }
    if (nvs_get_u32(handle, "tele_intv", &intv) == ESP_OK){
        if (intv >= 500 && intv <= 10000){
            g_telemetry_interval_ms = intv;
        }
    }
    nvs_close(handle);
    ESP_LOGI("TELE_NVS", "Loaded state: active=%d, interval=%lu ms", g_telemetry_active, (unsigned long)g_telemetry_interval_ms);
}

static void Azure_Offline_Sync_Task(void *pvParameters)
{
    ESP_LOGI("AZURE: OFFLINE SYNC", "Azure offline telemetry sync task started");

    while (1)
    {
        if (!Is_System_Internet_Connected() || !IoTHubHandle.isAzureInitialized || IoTHubHandle.isNeedReinit || bIsOtaActivated)
        {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        uint16_t unsynced_count = Fram_Log_Get_Unsynced_Count();
        if (unsynced_count == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        EnvLogRecord_t batch_records[10];
        uint16_t fetched_count = 0;

        if (Fram_Log_Get_Unsynced_Batch(batch_records, 10, &fetched_count) && fetched_count > 0)

        {
            cJSON *root = cJSON_CreateObject();
            cJSON *pl = cJSON_CreateObject();

            if (root != NULL && pl != NULL)
            {
                cJSON_AddStringToObject(pl, "HostName", IoTHubHandle.hostName);
                cJSON_AddStringToObject(pl, "DeviceId", IoTHubHandle.deviceId);
                cJSON_AddNumberToObject(pl, "Code", CMD_CODE_OFFLINE_TELEMETRY_SYNC);
                cJSON_AddNumberToObject(pl, "TimeStamp", (double)time(NULL));
                cJSON_AddBoolToObject(pl, "IsOfflineData", true);
                cJSON_AddNumberToObject(pl, "BatchSize", fetched_count);

                cJSON *arr = cJSON_CreateArray();
                if (arr != NULL)
                {
                    for (uint16_t i = 0; i < fetched_count; i++)
                    {
                        cJSON *rec_item = cJSON_CreateObject();
                        if (rec_item != NULL)
                        {
                            cJSON_AddNumberToObject(rec_item, "TimeStamp", (double)batch_records[i].timestamp);
                            cJSON_AddNumberToObject(rec_item, "ph", (double)(batch_records[i].ph_x100 / 100.0f));
                            cJSON_AddNumberToObject(rec_item, "temp", (double)(batch_records[i].temp_x100 / 100.0f));
                            cJSON_AddNumberToObject(rec_item, "do", (double)(batch_records[i].do_x100 / 100.0f));
                            cJSON_AddItemToArray(arr, rec_item);
                        }
                    }
                    cJSON_AddItemToObject(pl, "OfflineRecords", arr);
                }
                cJSON_AddItemToObject(root, "payload", pl);

                char *json_str = cJSON_PrintUnformatted(root);
                if (json_str != NULL)
                {
                    ESP_LOGI("AZURE: OFFLINE SYNC", "Sending %u offline records to Azure (remaining: %u)",
                             fetched_count, unsynced_count - fetched_count);

                    if (xSemaphoreTake(azureMutex, pdMS_TO_TICKS(5000U)) == pdTRUE)
                    {
                        AzureIoTResult_t send_res = AzureIoTHubClient_SendTelemetry(
                            &xAzureIoTHubClient,
                            (const uint8_t *)json_str,
                            strlen(json_str),
                            NULL,
                            eAzureIoTHubMessageQoS1,
                            NULL
                        );
                        xSemaphoreGive(azureMutex);

                        if (send_res == eAzureIoTSuccess)
                        {
                            ESP_LOGI("AZURE: OFFLINE SYNC", "Successfully sent batch of %u offline records", fetched_count);
                            Fram_Log_Commit_Synced_Count(fetched_count);
                        }
                        else
                        {
                            ESP_LOGE("AZURE: OFFLINE SYNC", "Send telemetry failed: %d. Will retry this batch...", send_res);
                        }
                    }
                    else
                    {
                        ESP_LOGE("AZURE: OFFLINE SYNC", "Cannot acquire mutex to send offline batch. Retrying...");
                    }
                    free(json_str);
                }
                cJSON_Delete(root);
            }

            vTaskDelay(pdMS_TO_TICKS(500));
        }

        else
        {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}


static void Azure_Telemetry_Task(void *pvParameters)
{
    prv_telemetry_state_load();
    uint32_t last_send_tick = xTaskGetTickCount();

    while (1)
    {
        /* ---- IDLE: Chờ cho đến khi backend gửi START ---- */
        if (!g_telemetry_active)
        {
            vTaskDelay(pdMS_TO_TICKS(500));  // Kiểm tra mỗi 500ms
            last_send_tick = xTaskGetTickCount();
            continue;
        }

        /* ---- Kiểm tra kết nối ---- */
        if (!Is_System_Internet_Connected() || !IoTHubHandle.isAzureInitialized
            || IoTHubHandle.isNeedReinit)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            last_send_tick = xTaskGetTickCount();
            continue;
        }        

        uint32_t now_tick = xTaskGetTickCount();
        uint32_t elapsed_ms = (now_tick - last_send_tick) * portTICK_PERIOD_MS;

        if (elapsed_ms >= g_telemetry_interval_ms)
        {
            last_send_tick = now_tick;

            PH_Temp_Sensor_Status_t status = Get_Sensor_Status();
            
            // 1. Lấy thông tin WiFi (SSID & RSSI)
            int16_t rssi = -120; 
            char wifi_ssid[32] = {0};
            wifi_config_manager_load(wifi_ssid, sizeof(wifi_ssid), NULL, 0);
            if (Is_System_Internet_Connected())
            {
                wifi_ap_record_t ap_info;
                if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
                {
                    rssi = ap_info.rssi;
                    if (wifi_ssid[0] == '\0')
                    {
                        strncpy(wifi_ssid, (const char *)ap_info.ssid, sizeof(wifi_ssid) - 1);
                    }
                }
            }

            // 2. Lấy thời gian Uptime (đơn vị: giây)
            uint32_t uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000ULL);

            // 3. Đọc và dịch nguyên nhân Reset
            esp_reset_reason_t reason = esp_reset_reason();
            const char *reason_str = "Unknown";
            switch (reason) {
                case ESP_RST_POWERON:   reason_str = "Power-on Reset"; break;
                case ESP_RST_EXT:       reason_str = "External Reset"; break;
                case ESP_RST_SW:        reason_str = "Software Reset"; break;
                case ESP_RST_PANIC:     reason_str = "Software Panic"; break;
                case ESP_RST_INT_WDT:   reason_str = "Interrupt Watchdog"; break;
                case ESP_RST_TASK_WDT:  reason_str = "Task Watchdog"; break;
                case ESP_RST_DEEPSLEEP: reason_str = "Deepsleep Reset"; break;
                case ESP_RST_BROWNOUT:  reason_str = "Brownout Reset"; break;
                default: break;
            }

            // 5. Định dạng chuỗi JSON gửi lên Azure (Nới rộng lên 768 bytes)
            char tele_str[768];
            int len = snprintf(tele_str, sizeof(tele_str),
                "{\"payload\":{"
                "\"HostName\":\"%s\","
                "\"DeviceId\":\"%s\","
                "\"Code\":504,"
                "\"TimeStamp\":%lld,"
                "\"SensorData\":{"
                    "\"ph\":%.2f,"
                    "\"ph_temp\":%.2f,"
                    "\"Valid\":%s,"
                    "\"do\":%.2f,"
                    "\"temp\":%.2f,"
                    "\"do_sat\":%.2f,"
                    "\"do_valid\":%s,"   
                    "\"rssi\":%d,"
                    "\"wifi_ssid\":\"%s\","
                    "\"uptime\":%lu,"
                    "\"reset_reason\":\"%s\","
                    "\"v_probe_mv\":%.2f,"
                    "\"do_err\":%d"
                "}"                        
            "}}",

                IoTHubHandle.hostName,
                IoTHubHandle.deviceId,
                (long long)time(NULL),
                (double)status.ph,
                (double)status.temperature,
                status.is_calibrated ? "true" : "false",
                (double)status.do_mg_l,
                (double)status.do_temp_c,
                (double)status.do_saturation_pct,
                status.do_valid ? "true" : "false",
                rssi,
                wifi_ssid,
                (unsigned long)uptime_sec,
                reason_str,
                (double)status.v_probe_mv,
                status.do_error_code
            );

            if (len > 0 && len < sizeof(tele_str))
            {
                ESP_LOGI("AZURE: TELEMETRY", "Push telemetry [Interval]: %s", tele_str);
                PushTelemetry(tele_str);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


/* Parse Azure IoT Hub connection string:
 * "HostName=xxx;DeviceId=yyy;SharedAccessKey=zzz"
 * Returns true if all 3 fields parsed successfully. */
static bool prv_parse_connection_string(const char *conn_str,
                                         char *host_out, size_t host_len,
                                         char *dev_out, size_t dev_len,
                                         char *key_out, size_t key_len)
{
    if (conn_str == NULL || host_out == NULL || dev_out == NULL || key_out == NULL)
        return false;

    /* Work on a mutable copy */
    size_t len = strlen(conn_str);
    if (len == 0)
        return false;

    char *buf = strdup(conn_str);
    if (buf == NULL)
        return false;

    bool found_host = false, found_dev = false, found_key = false;

    char *token = strtok(buf, ";");
    while (token != NULL)
    {
        /* Skip leading spaces */
        while (*token == ' ')
            token++;

        char *eq = strchr(token, '=');
        if (eq != NULL)
        {
            *eq = '\0';
            const char *key = token;
            const char *value = eq + 1;

            if (strcasecmp(key, "HostName") == 0)
            {
                strncpy(host_out, value, host_len - 1);
                host_out[host_len - 1] = '\0';
                found_host = true;
            }
            else if (strcasecmp(key, "DeviceId") == 0)
            {
                strncpy(dev_out, value, dev_len - 1);
                dev_out[dev_len - 1] = '\0';
                found_dev = true;
            }
            else if (strcasecmp(key, "SharedAccessKey") == 0)
            {
                strncpy(key_out, value, key_len - 1);
                key_out[key_len - 1] = '\0';
                found_key = true;
            }
        }

        token = strtok(NULL, ";");
    }

    free(buf);
    return (found_host && found_dev && found_key);
}

/* Push Telemetry to queue */
BaseType_t PushTelemetry(const char *payload)
{
    if (payload != NULL)
    {
        memset(g_last_telemetry_payload, 0, sizeof(g_last_telemetry_payload));
        strncpy(g_last_telemetry_payload, payload, sizeof(g_last_telemetry_payload) - 1);
    }

    if (IoTHubHandle.isTransmitInitialized && xQueueTelemetry != NULL)
    {
        TelemetryEvent_t event;
        memset(&event, 0, sizeof(event));
        strncpy(event.payload, payload, sizeof(event.payload) - 1);

        if (xQueueSend(xQueueTelemetry, &event, 0) == pdPASS)
        {
            return pdPASS;
        }
        else
        {
            ESP_LOGW("AZURE:", "Telemetry queue full, drop message");
            return pdFAIL;
        }
    }
    else
    {
        ESP_LOGW("AZURE: PUSH TELEMETRY TO QUEU", "Telemetry do not inited yet");
        return pdFAIL;
    }
}

static void prv_wifi_change_task(void *pvParameters)
{
    wifi_change_request_t *req = (wifi_change_request_t *)pvParameters;
    ESP_LOGI("AZURE: WIFI CFG", "Switching to SSID='%s'", req->ssid);

    /* Delay cho response kịp gửi */
    vTaskDelay(pdMS_TO_TICKS(2000));

    bool success = wifi_config_manager_set_with_fallback(
        req->ssid, req->pass, 30000);

    /* Push kết quả qua Telemetry */
    cJSON *tele = cJSON_CreateObject();
    cJSON *pl   = cJSON_CreateObject();
    if (tele != NULL && pl != NULL)
    {
        cJSON_AddItemToObject(tele, "payload", pl);
        cJSON_AddNumberToObject(pl, "Code", CMD_CODE_SET_DEVICE_CONFIG);
        cJSON_AddNumberToObject(pl, "TimeStamp", time(NULL));
        cJSON_AddStringToObject(pl, "Target", "wifi");
        cJSON_AddStringToObject(pl, "SSID", req->ssid);
        cJSON_AddBoolToObject(pl, "Success", success);
        cJSON_AddStringToObject(pl, "Message",
            success ? "WiFi changed successfully"
                    : "WiFi change FAILED, rolled back");

        char *s = cJSON_PrintUnformatted(tele);
        if (s != NULL)
        {
            /* Chờ Azure reconnect */
            for (int w = 0; !IoTHubHandle.isAzureInitialized && w < 15; w++)
                vTaskDelay(pdMS_TO_TICKS(1000));

            PushTelemetry(s);
            free(s);
        }
    }
    if (tele != NULL) cJSON_Delete(tele);
    else if (pl != NULL) cJSON_Delete(pl);

    /* Nếu WiFi mới OK → Azure cần reinit vì IP đổi */
    if (success) IoTHubHandle.isNeedReinit = true;

    free(req);
    vTaskDelete(NULL);
}

static void prv_reboot_task(void *pvParameters)
{
    ESP_LOGW("AZURE: REBOOT", "Rebooting in 2s...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}


/* Handle direct method */
void Azure_Handle_Direct_Method_Data(cJSON *payload, DirectMethodResponse_t *response)
{
    uint16_t _code = 0;

    cJSON *code = cJSON_GetObjectItem(payload, "Code");
    cJSON *data = cJSON_GetObjectItem(payload, "Data");

    if ((code != NULL) && (data != NULL))
    {
        _code = code->valueint;
        if (_code == CMD_CODE_UPDATE_FIRMWARE) // code == 501
        {
            cJSON *Version = cJSON_GetObjectItem(data, "Version");
            cJSON *Url = cJSON_GetObjectItem(data, "Url");

            if((Version != NULL) && cJSON_IsString(Version) && (Url != NULL) && cJSON_IsString(Url))
            {
                ESP_LOGI("AZURE: ", "---------- UPDATE FIRMWARE ----------");
                ESP_LOGI("AZURE: UPDATE FIRMWARE", "Version: %s, Url: %s\n", Version->valuestring, Url->valuestring);

                response->status = COMMAND_STATUS_OK;
                response->payloadLength = snprintf(response->payload, sizeof(response->payload), "Update version %s successfull", Version->valuestring);
                ESP_LOGI("AZURE: UPDATE FIRMWARE", "Preparing to update version %s", Version->valuestring);

                // Execute Telemetry First to Unblock Backend Thread Cleanly
                cJSON *tele = cJSON_CreateObject();
                cJSON *update_payload = cJSON_CreateObject();
                if(tele != NULL && update_payload != NULL)
                {
                    char *tele_str;
                    cJSON_AddNumberToObject(tele, "status", 200);
                    cJSON_AddItemToObject(tele, "payload", update_payload);
                    cJSON_AddNumberToObject(update_payload, "Code", 501);
                    cJSON_AddNumberToObject(update_payload, "TimeStamp", (double)time(NULL));
                    cJSON_AddStringToObject(update_payload, "Message", response->payload);

                    tele_str = cJSON_PrintUnformatted(tele);
                    if(tele_str != NULL)
                    {
                        PushTelemetry(tele_str);
                        free(tele_str);
                    }
                    else
                    {
                        ESP_LOGE("AZURE: CMD CALLBACK", "Telemetry json build failed");
                    }
                    ESP_LOGI("AZURE: UPDATE FIRMWARE", "Firmware dispatching to existing user_ota.c Daemon task...");
                }
                else
                {
                    ESP_LOGE("AZURE: CMD CALLBACK", "Telemetry alloc failed");
                }
                
                if(tele != NULL)
                {
                    cJSON_Delete(tele);
                }
                else if(update_payload != NULL)
                {
                    cJSON_Delete(update_payload);
                }

                // // Save URL and Signal event group for User_OTA_Task
                // memset(g_ota_update_url, 0, sizeof(g_ota_update_url));
                // strncpy(g_ota_update_url, Url->valuestring, sizeof(g_ota_update_url) - 1);
                
                // if(otaEventGroup != NULL)
                // {
                //     bIsOtaActivated = true;
                //     IoTHubHandle.isNeedReinit = true;
                //     xEventGroupSetBits(otaEventGroup, OTA_WAIT_BIT);
                // }
                User_Ota_Trigger(Url->valuestring);

            }
            else
            {
                response->status = COMMAND_STATUS_BAD_REQUEST;
                response->payloadLength = sprintf(response->payload, "Missing Version/Url");
                ESP_LOGE("AZURE: UPDATE FIRMWARE", "Missing Version or Url field");
            }
        }

        else if (_code == CMD_CODE_ASK_VERSION) // code == 502
        {
            ESP_LOGI("AZURE: ", "---------- ASK FOR VERSION ? ----------");

            response->status = COMMAND_STATUS_OK;
            response->payloadLength = snprintf(response->payload, sizeof(response->payload), "Firmware Version: %s", VERSION);

            time_t now = time(NULL);
            char formatted_now[32];
            char msg_str[32];

            prvFormatScheduleTime(now, formatted_now, sizeof(formatted_now));

            snprintf(msg_str, sizeof(msg_str), "Version %s ", VERSION);

            cJSON *tele = cJSON_CreateObject();
            cJSON *ask_payload = cJSON_CreateObject();
            if (tele != NULL && ask_payload != NULL)
            {
                cJSON_AddItemToObject(tele, "payload", ask_payload);
                cJSON_AddNumberToObject(ask_payload, "Code", CMD_CODE_ASK_VERSION);
                cJSON_AddStringToObject(ask_payload, "Time", formatted_now);
                cJSON_AddStringToObject(ask_payload, "Message", msg_str);

                char *tele_str = cJSON_PrintUnformatted(tele);
                if (tele_str != NULL)
                {
                    ESP_LOGI("AZURE: ASK VERSION", "Push version telemetry: %s", tele_str);
                    PushTelemetry(tele_str);
                    free(tele_str);
                }
                else
                {
                    ESP_LOGE("AZURE: ASK VERSION", "Telemetry json build failed");
                }
            }
            else
            {
                ESP_LOGE("AZURE: ASK VERSION", "Telemetry alloc failed");
            }
            if (tele != NULL)
            {
                cJSON_Delete(tele);
            }
            else if (ask_payload != NULL)
            {
                cJSON_Delete(ask_payload);
            }
        }

        else if (_code == CMD_CODE_GET_PH) // code == 503
        {
            ESP_LOGI("AZURE: ", "---------- GET PH ----------");

            cJSON *action = cJSON_GetObjectItem(data, "Action");

            if (action != NULL && cJSON_IsString(action))
            {
                /* ========== START ========== */
                if (strcasecmp(action->valuestring, "START") == 0)
                {
                    cJSON *interval = cJSON_GetObjectItem(data, "IntervalMs");
                    uint32_t interval_ms = 1000; // Giá trị mặc định (1 giây)

                    if (interval != NULL && cJSON_IsNumber(interval))
                    {
                        interval_ms = (uint32_t)interval->valueint;
                        if (interval_ms < 500)   interval_ms = 500;    // Clamp min 500ms
                        if (interval_ms > 10000) interval_ms = 10000;  // Clamp max 10 giây
                    }

                    g_telemetry_interval_ms = interval_ms;
                    g_telemetry_active = true;
                    prv_telemetry_state_save(g_telemetry_active, g_telemetry_interval_ms);

                    ESP_LOGI("AZURE: GET PH", "Telemetry STARTED, interval = %lu ms",
                             (unsigned long)interval_ms);

                    response->status = COMMAND_STATUS_OK;
                    response->payloadLength = snprintf(response->payload,
                        sizeof(response->payload),
                        "Telemetry started, interval=%lu ms", (unsigned long)interval_ms);

                    /* Push telemetry 1 lần ngay lập tức */
                    PH_Temp_Sensor_Status_t status = Get_Sensor_Status();

                    cJSON *tele = cJSON_CreateObject();
                    cJSON *tele_payload = cJSON_CreateObject();
                    if (tele != NULL && tele_payload != NULL)
                    {
                        cJSON_AddItemToObject(tele, "payload", tele_payload);
                        cJSON_AddNumberToObject(tele_payload, "Code", CMD_CODE_GET_PH);
                        cJSON_AddNumberToObject(tele_payload, "TimeStamp", time(NULL));
                        cJSON_AddStringToObject(tele_payload, "HostName", IoTHubHandle.hostName);
                        cJSON_AddStringToObject(tele_payload, "DeviceID", IoTHubHandle.deviceId);

                        cJSON_AddStringToObject(tele_payload, "Action", "START");
                        cJSON_AddNumberToObject(tele_payload, "IntervalMs", interval_ms);

                        cJSON_AddNumberToObject(tele_payload, "ph", status.ph);
                        cJSON_AddNumberToObject(tele_payload, "temp", status.temperature);
                        cJSON_AddNumberToObject(tele_payload, "v_probe_mv", status.v_probe_mv);
                        cJSON_AddBoolToObject(tele_payload, "is_calibrated", status.is_calibrated);
                        cJSON_AddNumberToObject(tele_payload, "filter_lvl", g_filter_level);

                        char *tele_str = cJSON_PrintUnformatted(tele);
                        if (tele_str != NULL)
                        {
                            ESP_LOGI("AZURE: GET PH", "Push ph telemetry: %s", tele_str);
                            PushTelemetry(tele_str);
                            free(tele_str);
                        }
                    }
                    else
                    {
                        ESP_LOGE("AZURE: GET PH", "Telemetry alloc failed");
                    }
                    if (tele != NULL)
                    {
                        cJSON_Delete(tele);
                    }
                    else if (tele_payload != NULL)
                    {
                        cJSON_Delete(tele_payload);
                    }
                }
                /* ========== STOP ========== */
                else if (strcasecmp(action->valuestring, "STOP") == 0)
                {
                    g_telemetry_active = false;
                    prv_telemetry_state_save(g_telemetry_active, g_telemetry_interval_ms);

                    ESP_LOGI("AZURE: GET PH", "Telemetry STOPPED");

                    response->status = COMMAND_STATUS_OK;
                    response->payloadLength = snprintf(response->payload,
                        sizeof(response->payload), "Telemetry stopped");
                }
                /* ========== UNKNOWN ACTION ========== */
                else
                {
                    ESP_LOGW("AZURE: GET PH", "Unknown action: %s", action->valuestring);
                    response->status = COMMAND_STATUS_BAD_REQUEST;
                    response->payloadLength = snprintf(response->payload,
                        sizeof(response->payload),
                        "Unknown action: %s", action->valuestring);
                }
            }
            else
            {
                /* One-shot mode */
                PH_Temp_Sensor_Status_t status = Get_Sensor_Status();
                response->status = COMMAND_STATUS_OK;
                response->payloadLength = snprintf(response->payload,
                    sizeof(response->payload), "OK");

                cJSON *tele = cJSON_CreateObject();
                cJSON *tele_payload = cJSON_CreateObject();
                if (tele != NULL && tele_payload != NULL)
                {
                    cJSON_AddItemToObject(tele, "payload", tele_payload);
                    cJSON_AddNumberToObject(tele_payload, "Code", CMD_CODE_GET_PH);
                    cJSON_AddNumberToObject(tele_payload, "TimeStamp", time(NULL));
                    cJSON_AddStringToObject(tele_payload, "HostName", IoTHubHandle.hostName);
                    cJSON_AddStringToObject(tele_payload, "DeviceID", IoTHubHandle.deviceId);

                    cJSON_AddNumberToObject(tele_payload, "ph", status.ph);
                    cJSON_AddNumberToObject(tele_payload, "temp", status.temperature);
                    cJSON_AddNumberToObject(tele_payload, "v_probe_mv", status.v_probe_mv);
                    cJSON_AddBoolToObject(tele_payload, "is_calibrated", status.is_calibrated);
                    cJSON_AddNumberToObject(tele_payload, "filter_lvl", g_filter_level);

                    char *tele_str = cJSON_PrintUnformatted(tele);
                    if (tele_str != NULL)
                    {
                        PushTelemetry(tele_str);
                        free(tele_str);
                    }
                }
                if (tele != NULL)
                {
                    cJSON_Delete(tele);
                }
            }    
        }
        else if (_code == CMD_CODE_PH_CALIBRATE) // code == 504
        {
            ESP_LOGI("AZURE: ", "---------- PH CALIBRATE ----------");
            cJSON *action = cJSON_GetObjectItem(data, "Action");
            cJSON *target_ph_item = cJSON_GetObjectItem(data, "TargetPH");
            cJSON *cal_type_item = cJSON_GetObjectItem(data, "CalType");
            
            PH_Temp_Sensor_Status_t status = Get_Sensor_Status();
            bool success = false;
            float target_ph = 0.0f;
            uint8_t cal_type = 2;

            if (target_ph_item != NULL && cJSON_IsNumber(target_ph_item) && cal_type_item != NULL && cJSON_IsNumber(cal_type_item))
            {
                target_ph = (float)target_ph_item->valuedouble;
                cal_type = (uint8_t)cal_type_item->valueint;
                success = Calibrate_PH_Point(target_ph, status.v_probe_mv, status.temperature, cal_type);
                if (success)
                {
                    response->status = COMMAND_STATUS_OK;
                    response->payloadLength = snprintf(response->payload, sizeof(response->payload),
                        "Calibrate pH %.2f (type %d) OK", target_ph, cal_type);
                }
                else
                {
                    response->status = COMMAND_STATUS_BAD_REQUEST;
                    response->payloadLength = snprintf(response->payload, sizeof(response->payload),
                        "Calibrate pH %.2f (type %d) failed", target_ph, cal_type);
                }
            }
            else if (action != NULL && cJSON_IsString(action))
            {
                bool action_valid = true;
                bool is_reset_action = false;
                if (strcasecmp(action->valuestring, "CAL_7") == 0 || strcasecmp(action->valuestring, "CAL_7_2PT") == 0)
                {
                    target_ph = 7.00f;
                    cal_type = 2;
                }
                else if (strcasecmp(action->valuestring, "CAL_4") == 0 || strcasecmp(action->valuestring, "CAL_4_2PT") == 0)
                {
                    target_ph = 4.00f;
                    cal_type = 2;
                }
                else if (strcasecmp(action->valuestring, "CAL_4_3PT") == 0)
                {
                    target_ph = 4.00f;
                    cal_type = 3;
                }
                else if (strcasecmp(action->valuestring, "CAL_686_3PT") == 0)
                {
                    target_ph = 6.86f;
                    cal_type = 3;
                }
                else if (strcasecmp(action->valuestring, "CAL_918_3PT") == 0)
                {
                    target_ph = 9.18f;
                    cal_type = 3;
                }
                else if (strcasecmp(action->valuestring, "CAL_7_3PT") == 0)
                {
                    target_ph = 7.00f;
                    cal_type = 3;
                }
                else if (strcasecmp(action->valuestring, "CAL_10_3PT") == 0)
                {
                    target_ph = 10.00f;
                    cal_type = 3;
                }
                else if (strcasecmp(action->valuestring, "RESET_PH_SENSOR") == 0)
                {
                    is_reset_action = true;
                }
                else
                {
                    action_valid = false;
                }

                if (is_reset_action)
                {
                    success = Reset_PH_Calibration();
                    if (success)
                    {
                        response->status = COMMAND_STATUS_OK;
                        response->payloadLength = snprintf(response->payload, sizeof(response->payload),
                            "Reset pH sensor calibration OK");
                    }
                    else
                    {
                        response->status = COMMAND_STATUS_BAD_REQUEST;
                        response->payloadLength = snprintf(response->payload, sizeof(response->payload),
                            "Reset pH sensor calibration failed");
                    }
                }
                else if (action_valid)
                {
                    success = Calibrate_PH_Point(target_ph, status.v_probe_mv, status.temperature, cal_type);
                    if (success)
                    {
                        response->status = COMMAND_STATUS_OK;
                        response->payloadLength = snprintf(response->payload, sizeof(response->payload),
                            "Calibrate pH %.2f (Action %s) OK", target_ph, action->valuestring);
                    }
                    else
                    {
                        response->status = COMMAND_STATUS_BAD_REQUEST;
                        response->payloadLength = snprintf(response->payload, sizeof(response->payload),
                            "Calibrate pH %.2f (Action %s) failed", target_ph, action->valuestring);
                    }
                }
                else
                {
                    ESP_LOGW("AZURE: PH CALIBRATE", "Unknown action: %s", action->valuestring);
                    response->status = COMMAND_STATUS_BAD_REQUEST;
                    response->payloadLength = snprintf(response->payload, sizeof(response->payload),
                        "Unknown action: %s", action->valuestring);
                }
            }
            else
            {
                response->status = COMMAND_STATUS_BAD_REQUEST;
                response->payloadLength = snprintf(response->payload, sizeof(response->payload),
                    "Missing Action or TargetPH + CalType fields");
            }
        }
        else if (_code == CMD_CODE_SET_DEVICE_CONFIG) // code == 505
        {
            ESP_LOGI("AZURE: ", "---------- SET DEVICE CONFIG ----------");

            /* ---- Xác thực Token ---- */
            cJSON *token_item = cJSON_GetObjectItem(data, "Token");
            if (token_item == NULL || !cJSON_IsString(token_item)
                || !auth_secret_verify(token_item->valuestring))
            {
                response->status = 401;
                response->payloadLength = snprintf(response->payload,
                    sizeof(response->payload), "Unauthorized: invalid Token");
            }
            else
            {
                /* ---- Phân loại Target ---- */
                cJSON *target = cJSON_GetObjectItem(data, "Target");

                if (target == NULL || !cJSON_IsString(target))
                {
                    response->status = COMMAND_STATUS_BAD_REQUEST;
                    response->payloadLength = snprintf(response->payload,
                        sizeof(response->payload),
                        "Missing Target (expected 'wifi' or 'azure')");
                }
                /* ========== Target: wifi ========== */
                else if (strcasecmp(target->valuestring, "wifi") == 0)
                {
                    cJSON *ssid_item = cJSON_GetObjectItem(data, "SSID");
                    cJSON *pass_item = cJSON_GetObjectItem(data, "Password");

                    if (!ssid_item || !cJSON_IsString(ssid_item)
                        || strlen(ssid_item->valuestring) == 0)
                    {
                        response->status = COMMAND_STATUS_BAD_REQUEST;
                        response->payloadLength = snprintf(response->payload,
                            sizeof(response->payload), "Missing or empty SSID");
                    }
                    else
                    {
                        wifi_change_request_t *req = calloc(1, sizeof(*req));
                        if (req == NULL)
                        {
                            response->status = COMMAND_STATUS_DEVICE_ERROR;
                            response->payloadLength = snprintf(response->payload,
                                sizeof(response->payload), "Out of memory");
                        }
                        else
                        {
                            strncpy(req->ssid, ssid_item->valuestring,
                                    sizeof(req->ssid) - 1);
                            if (pass_item && cJSON_IsString(pass_item))
                                strncpy(req->pass, pass_item->valuestring,
                                        sizeof(req->pass) - 1);

                            if (xTaskCreatePinnedToCore(prv_wifi_change_task,
                                    "wifi_chg", 4096, req, 3, NULL, 0) == pdPASS)
                            {
                                response->status = COMMAND_STATUS_OK;
                                response->payloadLength = snprintf(response->payload,
                                    sizeof(response->payload),
                                    "Reconnecting to '%s'...", req->ssid);
                            }
                            else
                            {
                                free(req);
                                response->status = COMMAND_STATUS_DEVICE_ERROR;
                                response->payloadLength = snprintf(response->payload,
                                    sizeof(response->payload),
                                    "Create wifi task failed");
                            }
                        }
                    }
                }
                /* ========== Target: filter ========== */
                else if (strcasecmp(target->valuestring, "filter") == 0)
                {
                    cJSON *level_item = cJSON_GetObjectItem(data, "Level");
                    if (level_item == NULL || !cJSON_IsNumber(level_item))
                    {
                        response->status = COMMAND_STATUS_BAD_REQUEST;
                        response->payloadLength = snprintf(response->payload,
                            sizeof(response->payload), "Missing or invalid Level");
                    }
                    else
                    {
                        uint32_t lvl = (uint32_t)level_item->valueint;
                        if (lvl >= FILTER_LEVEL_COUNT)
                        {
                            response->status = COMMAND_STATUS_BAD_REQUEST;
                            response->payloadLength = snprintf(response->payload,
                                sizeof(response->payload), "Invalid Level (expected 0, 1 or 2)");
                        }
                        else
                        {
                            g_filter_level = (filter_level_t)lvl;
                            Nvs_Write_Number("filter_lvl", (uint32_t)g_filter_level);
                            update_system_filters_level(g_filter_level);
                            
                            response->status = COMMAND_STATUS_OK;
                            response->payloadLength = snprintf(response->payload,
                                sizeof(response->payload), "Set filter level to %d OK", (int)lvl);
                        }
                    }
                }
                /* ========== Target: azure ========== */
                else if (strcasecmp(target->valuestring, "azure") == 0)
                {
                    char host[IOT_HUB_HOST_NAME_LEN] = {0};
                    char dev[IOT_HUB_DEVICE_ID_LEN] = {0};
                    char sym[IOT_HUB_SYMMETRIC_KEY_LEN] = {0};
                    bool has_credentials = false;

                    /* ---- Thử parse ConnectionString trước ---- */
                    cJSON *conn_str = cJSON_GetObjectItem(data, "ConnectionString");
                    if (conn_str != NULL && cJSON_IsString(conn_str) && strlen(conn_str->valuestring) > 0)
                    {
                        if (prv_parse_connection_string(conn_str->valuestring,
                                host, sizeof(host),
                                dev, sizeof(dev),
                                sym, sizeof(sym)))
                        {
                            ESP_LOGI("AZURE: SET DEVICE CFG",
                                     "Parsed ConnectionString -> Host:%s Dev:%s", host, dev);
                            has_credentials = true;
                        }
                        else
                        {
                            ESP_LOGW("AZURE: SET DEVICE CFG",
                                     "ConnectionString parse failed, falling back to individual fields");
                        }
                    }

                    /* ---- Fallback: dùng các trường riêng lẻ (backward compatible) ---- */
                    if (!has_credentials)
                    {
                        cJSON *host_item = cJSON_GetObjectItem(data, "HostName");
                        cJSON *dev_item  = cJSON_GetObjectItem(data, "DeviceId");
                        cJSON *sym_item  = cJSON_GetObjectItem(data, "SymmetricKey");

                        if (host_item && cJSON_IsString(host_item) && strlen(host_item->valuestring) > 0
                         && dev_item  && cJSON_IsString(dev_item)  && strlen(dev_item->valuestring) > 0
                         && sym_item  && cJSON_IsString(sym_item)  && strlen(sym_item->valuestring) > 0)
                        {
                            strncpy(host, host_item->valuestring, sizeof(host) - 1);
                            strncpy(dev, dev_item->valuestring, sizeof(dev) - 1);
                            strncpy(sym, sym_item->valuestring, sizeof(sym) - 1);
                            has_credentials = true;
                        }
                    }

                    if (!has_credentials)
                    {
                        response->status = COMMAND_STATUS_BAD_REQUEST;
                        response->payloadLength = snprintf(response->payload,
                            sizeof(response->payload),
                            "Missing ConnectionString or HostName/DeviceId/SymmetricKey");
                    }
                    else
                    {
                        bool saved = azure_config_manager_save(host, dev, sym);

                        if (saved)
                        {
                            memset(IoTHubHandle.hostName, 0, sizeof(IoTHubHandle.hostName));
                            memset(IoTHubHandle.deviceId, 0, sizeof(IoTHubHandle.deviceId));
                            memset(IoTHubHandle.symmetricKey, 0, sizeof(IoTHubHandle.symmetricKey));
                            strncpy(IoTHubHandle.hostName, host,
                                    sizeof(IoTHubHandle.hostName) - 1);
                            strncpy(IoTHubHandle.deviceId, dev,
                                    sizeof(IoTHubHandle.deviceId) - 1);
                            strncpy(IoTHubHandle.symmetricKey, sym,
                                    sizeof(IoTHubHandle.symmetricKey) - 1);

                            response->status = COMMAND_STATUS_OK;
                            response->payloadLength = snprintf(response->payload,
                                sizeof(response->payload),
                                "Azure updated to '%s'. Reinitializing...",
                                dev);

                            IoTHubHandle.isNeedReinit = true;
                        }
                        else
                        {
                            response->status = COMMAND_STATUS_DEVICE_ERROR;
                            response->payloadLength = snprintf(response->payload,
                                sizeof(response->payload), "NVS save failed");
                        }
                    }
                }
                else if (strcasecmp(target->valuestring, "rs485") == 0){
                    response->status = COMMAND_STATUS_BAD_REQUEST;
                    response->payloadLength = snprintf(response->payload,
                        sizeof(response->payload),
                        "Baudrate configuration not supported on pH/Temp sensor");
                }
                /* ========== Unknown target ========== */
                else
                {
                    response->status = COMMAND_STATUS_BAD_REQUEST;
                    response->payloadLength = snprintf(response->payload,
                        sizeof(response->payload),
                        "Unknown Target '%s'", target->valuestring);
                }
            }
        }

        else if (_code == CMD_CODE_DO_CALIBRATE)
        {
            ESP_LOGI("AZURE: ", "---------- DO CALIBRATE ----------");
            cJSON *action = cJSON_GetObjectItem(data, "Action");
            cJSON *value_item = cJSON_GetObjectItem(data, "Value");
            if (action != NULL && cJSON_IsString(action))
            {
                bool action_valid = true;
                esp_err_t err = ESP_OK;
                if (strcasecmp(action->valuestring, "CAL_DO_ZERO") == 0)
                {
                    err = do_sensor_calibrate_zero();
                }
                else if (strcasecmp(action->valuestring, "CAL_DO_SLOPE") == 0)
                {
                    err = do_sensor_calibrate_slope();
                }
                else if (strcasecmp(action->valuestring, "CORRECT_DO_TEMP") == 0)
                {
                    if (value_item != NULL && cJSON_IsNumber(value_item))
                    {
                        err = do_sensor_correct_temp((float)value_item->valuedouble);
                    }
                    else
                    {
                        action_valid = false;
                        response->status = COMMAND_STATUS_BAD_REQUEST;
                        response->payloadLength = snprintf(response->payload, sizeof(response->payload),
                            "Missing Value field for CORRECT_DO_TEMP");
                    }
                }
                else if (strcasecmp(action->valuestring, "COMPENSATE_SALINITY") == 0)
                {
                    if (value_item != NULL && cJSON_IsNumber(value_item))
                    {
                        err = do_sensor_set_salinity((float)value_item->valuedouble);
                    }
                    else
                    {
                        action_valid = false;
                        response->status = COMMAND_STATUS_BAD_REQUEST;
                        response->payloadLength = snprintf(response->payload, sizeof(response->payload),
                            "Missing Value field for COMPENSATE_SALINITY");
                    }
                }
                else if (strcasecmp(action->valuestring, "RESET_DO_SENSOR") == 0)
                {
                    err = do_sensor_reset();
                }
                else
                {
                    action_valid = false;
                    response->status = COMMAND_STATUS_BAD_REQUEST;
                    response->payloadLength = snprintf(response->payload, sizeof(response->payload),
                        "Unknown Action: %s", action->valuestring);
                }
                if (action_valid)
                {
                    if (err == ESP_OK)
                    {
                        response->status = COMMAND_STATUS_OK;
                        response->payloadLength = snprintf(response->payload, sizeof(response->payload),
                            "DO Calibrate Action %s OK", action->valuestring);
                    }
                    else
                    {
                        response->status = COMMAND_STATUS_BAD_REQUEST;
                        response->payloadLength = snprintf(response->payload, sizeof(response->payload),
                            "DO Calibrate Action %s failed: %d", action->valuestring, err);
                    }
                }
            }
            else
            {
                response->status = COMMAND_STATUS_BAD_REQUEST;
                response->payloadLength = snprintf(response->payload, sizeof(response->payload),
                    "Missing or invalid Action field");
            }
        }
        else if (_code == CMD_CODE_REBOOT)
        {
            ESP_LOGI("AZURE: ", "---------- REBOOT ----------");
            response->status = COMMAND_STATUS_OK;
            response->payloadLength = snprintf(response->payload, sizeof(response->payload),
                "Device is rebooting...");

            if (xTaskCreatePinnedToCore(prv_reboot_task, "reboot_task", 2048, NULL, 3, NULL, 0) != pdPASS)
            {
                ESP_LOGE("AZURE: REBOOT", "Failed to create reboot task, restarting immediately");
                esp_restart();
            }
        }
        else if (_code == CMD_CODE_GET_FULL_TELEMETRY) // code == 508
        {
            ESP_LOGI("AZURE: ", "---------- GET FULL TELEMETRY (508) ----------");
            PH_Temp_Sensor_Status_t status = Get_Sensor_Status();

            int16_t rssi = -120;
            char wifi_ssid[32] = {0};
            char wifi_pass[64] = {0};
            wifi_config_manager_load(wifi_ssid, sizeof(wifi_ssid), wifi_pass, sizeof(wifi_pass));
            if (Is_System_Internet_Connected())
            {
                wifi_ap_record_t ap_info;
                if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
                {
                    rssi = ap_info.rssi;
                    if (wifi_ssid[0] == '\0')
                    {
                        strncpy(wifi_ssid, (const char *)ap_info.ssid, sizeof(wifi_ssid) - 1);
                    }
                }
            }

            uint32_t uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000ULL);

            esp_reset_reason_t reason = esp_reset_reason();
            const char *reason_str = "Unknown";
            switch (reason) {
                case ESP_RST_POWERON:   reason_str = "Power-on Reset"; break;
                case ESP_RST_EXT:       reason_str = "External Reset"; break;
                case ESP_RST_SW:        reason_str = "Software Reset"; break;
                case ESP_RST_PANIC:     reason_str = "Software Panic"; break;
                case ESP_RST_INT_WDT:   reason_str = "Interrupt Watchdog"; break;
                case ESP_RST_TASK_WDT:  reason_str = "Task Watchdog"; break;
                case ESP_RST_DEEPSLEEP: reason_str = "Deepsleep Reset"; break;
                case ESP_RST_BROWNOUT:  reason_str = "Brownout Reset"; break;
                default: break;
            }

            uint32_t free_ram = esp_get_free_heap_size();
            const esp_partition_t *running_part = esp_ota_get_running_partition();
            const char *part_label = (running_part != NULL) ? running_part->label : "Unknown";

            char tele_str[1024];
            int len = snprintf(tele_str, sizeof(tele_str),
                "{\"payload\":{"
                "\"HostName\":\"%s\","
                "\"DeviceId\":\"%s\","
                "\"Code\":508,"
                "\"TimeStamp\":%lld,"
                "\"SensorData\":{"
                    "\"ph\":%.2f,"
                    "\"ph_temp\":%.2f,"
                    "\"Valid\":%s,"
                    "\"do\":%.2f,"
                    "\"temp\":%.2f,"
                    "\"do_sat\":%.2f,"
                    "\"do_valid\":%s,"   
                    "\"rssi\":%d,"
                    "\"wifi_ssid\":\"%s\","
                    "\"wifi_pass\":\"%s\","
                    "\"uptime\":%lu,"
                    "\"reset_reason\":\"%s\","
                    "\"free_ram\":%lu,"
                    "\"v_probe_mv\":%.2f,"
                    "\"do_err\":%d,"
                    "\"ver\":\"%s\","
                    "\"ota_part\":\"%s\","
                    "\"ph_calib\":{"
                        "\"is_calibrated\":%s,"
                        "\"cal_type\":%d,"
                        "\"ph7_mv\":%.2f,"
                        "\"ph7_temp\":%.2f,"
                        "\"ph4_mv\":%.2f,"
                        "\"ph4_temp\":%.2f,"
                        "\"ph10_mv\":%.2f,"
                        "\"ph10_temp\":%.2f,"
                        "\"slope_norm\":%.3f,"
                        "\"slope_high\":%.3f,"
                        "\"u7\":%.4f"
                    "}"
                "}"                        
            "}}",
                IoTHubHandle.hostName,
                IoTHubHandle.deviceId,
                (long long)time(NULL),
                (double)status.ph,
                (double)status.temperature,
                status.is_calibrated ? "true" : "false",
                (double)status.do_mg_l,
                (double)status.do_temp_c,
                (double)status.do_saturation_pct,
                status.do_valid ? "true" : "false",
                rssi,
                wifi_ssid,
                wifi_pass,
                (unsigned long)uptime_sec,
                reason_str,
                (unsigned long)free_ram,
                (double)status.v_probe_mv,
                status.do_error_code,
                VERSION,
                part_label,
                ph_cal.is_calibrated ? "true" : "false",
                ph_cal.cal_type,
                (double)ph_cal.ph7_voltage_mv,
                (double)ph_cal.ph7_temp_c,
                (double)ph_cal.ph4_voltage_mv,
                (double)ph_cal.ph4_temp_c,
                (double)ph_cal.ph10_voltage_mv,
                (double)ph_cal.ph10_temp_c,
                (double)ph_cal.slope_norm,
                (double)ph_cal.slope_high,
                (double)ph_cal.u7
            );

            if (len > 0 && len < sizeof(tele_str))
            {
                ESP_LOGI("AZURE: TELEMETRY (508)", "Requested full telemetry: %s", tele_str);
                // PushTelemetry(tele_str); // Tắt push telemetry cho Code 508 (chỉ gửi trực tiếp qua Direct Method Response)
                response->status = COMMAND_STATUS_OK;
                snprintf(response->payload, sizeof(response->payload), "Full telemetry sent successfully");
            }
            else
            {
                response->status = COMMAND_STATUS_DEVICE_ERROR;
                snprintf(response->payload, sizeof(response->payload), "Failed to format telemetry JSON");
            }
        }

        else
        {
            response->status = COMMAND_STATUS_BAD_REQUEST;
            response->payloadLength = sprintf(response->payload, "Unknown command code");
            ESP_LOGE("AZURE: ", "Unknown command code: %d", _code);
        }
    }
    else
    {
        ESP_LOGE("AZURE: CMD CALLBACK", "Missing code or data field");
        response->status = COMMAND_STATUS_BAD_REQUEST;
        response->payloadLength = sprintf(response->payload, "Missing code or data feild");
    }

    ESP_LOGI("AZURE: ", "---------- END CMD CALLBACK ----------\n\n\n");
}