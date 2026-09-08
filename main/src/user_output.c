#include "user_ouput.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "stdint.h"
#include "string.h"
#include "driver/i2c_master.h"
#include "user_system.h"
#include "cJSON.h"
#include "user_crc32.h"
#include "user_fram.h"
#include "user_azure.h"
#include "time.h"
#include "user_fram.h"

void User_Input_Task(void);

#define IO_CLK_HIGH()       gpio_set_level(IO_CLK_PIN, 1)
#define IO_CLK_LOW()        gpio_set_level(IO_CLK_PIN, 0)

#define IO_STR_HIGH()       gpio_set_level(IO_STR_PIN, 1)
#define IO_STR_LOW()        gpio_set_level(IO_STR_PIN, 0)


#define  IO_CLK_PIN         16
#define  IO_MOSI_PIN        17
#define  IO_STR_PIN         18
#define  OUTPUT_ENABLE_PIN  15

#define DEVICE_SIZE           sizeof(Device_t)

const char *IO_TAG = "IO OUTPUT: ";
const char *IO_INPUT_TAG = "IO_INPUT";

// static bool isFramLoadedAndPrinted = false;


Device_Handle_t DeviceHandle;
IO_Input_Config_Handle_t InputConfHandle;
EventGroupHandle_t IO_Event_Group;
// static uint32_t outputBuf;

uint8_t isGetNoticeCounter = 0;
/* Input function Prototypes */

int User_Device_Get_Index_By_Id(uint16_t deviceId)
{
    for(int i = 0; i < DEVICE_MAX_NUM; i++)
    {
        if(DeviceHandle.Device[i].id == deviceId)
        {
            return i;
        }
    }
    return -1;
}


/* Functions for input/output configuration */

// void User_Out_Put_IO_Config(void)
// {
//     gpio_config_t io_conf = {};
//     io_conf.intr_type = GPIO_INTR_DISABLE;
//     io_conf.mode = GPIO_MODE_OUTPUT;
//     io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
//     io_conf.pull_down_en = 1;
//     io_conf.pull_up_en = 0;
//     gpio_config(&io_conf);
    
//     gpio_set_level(OUTPUT_ENABLE_PIN, 0); // Set output enable pin low to enable output
// }

void User_Input_IO_Config(void)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;

    gpio_config(&io_conf);
}

bool User_Input_Check_Is_Active(gpio_num_t io)
{
    if(gpio_get_level(io) == IO_CONFIG_INPUT_ACTIVE)
    {
        return true;
    }else{
        return false;
    }
}

void IO_Driver_Task(void)
{
    // Task chỉ thực hiện cấu hình khởi tạo I/O cứng, sau đó duy trì sống để không gây crash app_main
    User_Out_Put_IO_Config();

    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
