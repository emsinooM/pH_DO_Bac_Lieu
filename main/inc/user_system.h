#pragma once

#include "tcp_server_com.h"

#include "stdint.h"
#include "stdbool.h"
#include "time.h"

// #define CRC32_BYPASS        1

#define START_OF_FRAME 0x02
#define END_OF_FRAME    0x03

#define SYS_SERVER_IP_DEFAULT   "192.168.1.100"
#define SYS_SERVER_PORT_DEFAULT   3000
#define SYS_WIFI_SSID_DEFAULT   "Mebieco 2.4G"
#define SYS_WIFI_PASS_DEFAULT   "68686868@"


#define SYS_IOT_HUB_HOST_NAME_DEFAULT           "dev-iot-hub-1.azure-devices.net"
#define SYS_IOT_HUB_DEVICE_ID_DEFAULT           "test-devices-6"
#define SYS_IOT_HUB_SYMMETRIC_KEY_DEFAULT       "S7cpXyl9xfxLNUBMR4D36aKJhlf1IQVoFk8swv62fKI="

/* Define for FRAM */
#define DEVICE_CONFIG_START_ADDRESS 0x1000
#define DEVICE_SPACE_LEN_IN_FRAM    512
#define NUM_OF_DEVICE               4

/* Define cmd code */
#define CMD_CODE_UPDATE_FIRMWARE            501
#define CMD_CODE_ASK_VERSION                502
#define CMD_CODE_GET_PH                     503
#define CMD_CODE_PH_CALIBRATE               504
#define CMD_CODE_SET_DEVICE_CONFIG          505


#define VERSION "1.0.2"


/* Define Telemetry queue length */
#define TELEMETRY_QUEUE_LENGTH  10

typedef struct 
{
    /* data */
    bool isWifiConnected;
    bool isTimeSync;

    bool isTimeSyncCb;
    time_t epochtime;
}Sys_Info_Handle_t;

extern Sys_Info_Handle_t Sys_Info;
extern uint32_t reset_count;

void User_System_Get_Config(void);

void User_System_Init(void);

bool Is_System_Time_Synchronized(void);

bool Is_System_Internet_Connected(void);

void User_System_Clear_Reset_Count(void);
