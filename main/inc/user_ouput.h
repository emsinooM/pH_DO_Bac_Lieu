#pragma once

#include "stdint.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "stdbool.h"
#include "time.h"

/* Define maximum number of device */
#define DEVICE_MAX_NUM  4
/* Define address */
#define FRAM_DEVICE_ADDR   0x0000
#define FRAM_DEVICE_SIZE   512 //sizeof(Device_Parameters_t)
/* Max schedules per device */
#define DEVICE_SCHEDULE_MAX 15
/* Define for input */
#define IO_CONFIG_INPUT_0   20
#define IO_CONFIG_INPUT_1   19

#define IO_CONFIG_INPUT_ACTIVE  0
#define IO_CONFIG_INPUT_DE_ACTIVE  1



/* Define for output */
#define SERVER_TRIGGER_IO_BIT   (1<<0)
#define IO_LED_EXTBOARD_PIN    41



#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<IO_LED_EXTBOARD_PIN) | (1ULL<<IO_CLK_PIN) | (1ULL<<IO_MOSI_PIN) | (1ULL<<IO_STR_PIN) | (1ULL<<OUTPUT_ENABLE_PIN))

#define GPIO_INPUT_PIN_SEL  ((1ULL<<IO_CONFIG_INPUT_0) | (1ULL<<IO_CONFIG_INPUT_1))
#define IO_LED_PIN      42
#define IO_10_TEST      10




/* Define name and id for devices */
#define DEVICE_NAME_PADDLEWHEEL_1   "PaddleWheel_1"
#define DEVICE_NAME_PADDLEWHEEL_2   "PaddleWheel_2"
#define DEVICE_NAME_AIRBLOW         "AirBlow"
#define DEVICE_NAME_SYPHON          "Syphon"

#define DEVICE_ID_PADDLEWHEEL_1   11
#define DEVICE_ID_PADDLEWHEEL_2   12
#define DEVICE_ID_AIRBLOW         21
#define DEVICE_ID_SYPHON          41

typedef enum
{
    DEVICE_INDEX_PADDLEWHEEL_1 = 0,
    DEVICE_INDEX_PADDLEWHEEL_2,
    DEVICE_INDEX_AIRBLOW,
    DEVICE_INDEX_SYPHON
} Device_Index_t;




typedef enum
{
    DEVICE_STATE_OFF = 0,
    DEVICE_STATE_ON,
    DEVICE_STATE_FAULT
}Device_State_t;

typedef enum
{
    DEVICE_ACTIVE_TYPE_NONE =0,
    DEVICE_ACTIVE_TYPE_TRIGGER = 1,
    DEVICE_ACTIVE_TYPE_SCHEDULE = 2,
    DEVICE_ACTIVE_TYPE_CONFIG =3,
    DEVICE_ACTIVE_TYPE_UPDATE_FW = 4
}Device_ActiveType_t;

typedef struct __attribute__((packed))
{
    time_t startTime;
    time_t stopTime;
    uint8_t runTime;
    uint16_t pauseTime;
    uint8_t isFinished;
}Device_Schedule_t;

typedef struct __attribute__((packed))
{
    bool isActived;
    bool isScheduled;

    
    
    time_t startTime;
    time_t stopTime;
    uint32_t duration;

    TickType_t stopAtTick;
    uint16_t id;
    Device_State_t state;
    uint8_t index;
    uint32_t weight;
    char name[32];
    uint8_t RunTime;
    uint16_t PauseTime;
    uint8_t scheduleCount;
    uint8_t scheduleIndex;
    Device_Schedule_t schedules[DEVICE_SCHEDULE_MAX];
}Device_Parameters_t;

typedef struct 
{
    bool isInputChecked;
    bool isTriggered;
    bool isNeedOnTime;
    uint8_t inputBuf;
    uint32_t outputBuf;
    Device_ActiveType_t activeType;
    Device_Parameters_t Device[DEVICE_MAX_NUM];
}Device_Handle_t;

typedef struct __attribute__((packed))
{
    uint16_t id;
    uint8_t index;
    uint8_t isScheduled;
    Device_State_t state;
    time_t startTime;
    time_t stopTime;
    uint8_t runTime;
    uint16_t pauseTime;
    uint8_t scheduleCount;
    uint8_t scheduleIndex;
    Device_Schedule_t schedules[DEVICE_SCHEDULE_MAX];
}Device_Persist_t;

typedef struct 
{
    bool isConfigMode;
}IO_Input_Config_Handle_t;

// typedef struct
// {
//     uint8_t DeviceId;
//     char DeviceName[32];
//     uint8_t Index;
//     bool isActived;
// } device_added_t;

// extern device_added_t added_device_list[DEVICE_MAX_NUM];

extern Device_Handle_t DeviceHandle;
extern IO_Input_Config_Handle_t InputConfHandle;
extern EventGroupHandle_t IO_Event_Group;

void User_Out_Put_IO_Config(void);

void User_Input_IO_Config(void);

bool User_Input_Check_Is_Active(gpio_num_t io);

void User_Output_Set_Value(uint8_t value, uint8_t deviceIndex, Device_Handle_t *handle);

void User_Out_Put_Flush_All(uint8_t state);

void User_Output_Shift_Data(const uint32_t *data);

void User_Output_Parse_Buffer(Device_Handle_t *handle);

void User_Output_Deploy(Device_Handle_t *handle);

void User_Device_Init(void);

int User_Device_Get_Index_By_Id(uint16_t deviceId);

void User_Output_Polling(Device_Handle_t *handle);

void IO_Driver_Task(void);
