#include "user_fram.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "string.h"
#include "user_ouput.h"
#include "user_azure.h"

#define MAX_DEVICE          10
#define DEVICE_NAME_LEN     16
#define FRAM_START   0x0000

static bool s_fram_initialized = false;

spi_device_handle_t spiFram;

const char *FRAM_TAG = "FRAM";

uint8_t  cs_pin = 0;


void spi_post_transfer_callback(spi_transaction_t *t)
{
    uint8_t cs=*((uint8_t*)t->user);
    gpio_set_level(cs, 1);
    // ESP_LOGI("SPI", "Cs active");
}

void spi_pre_transfer_callback(spi_transaction_t *t)
{
    uint8_t cs=*((uint8_t*)t->user);
    gpio_set_level(cs, 0);
    // ESP_LOGI("SPI", "Cs deactive");
}


static bool spi_master_init() 
{

    // cs_pin = PIN_NUM_CS;
    // gpio_reset_pin(PIN_NUM_CS);         //configures the IOMUX for this pin to the GPIO function

    // gpio_set_pull_mode(PIN_NUM_CS, GPIO_PULLUP_ONLY);
    // gpio_set_direction(PIN_NUM_CS, GPIO_MODE_OUTPUT);
    // gpio_set_level(PIN_NUM_CS, 1);


    esp_err_t ret;
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1, // Không dùng QuadWP
        .quadhd_io_num = -1, // Không dùng QuadHD
        .max_transfer_sz = 1024 // Kích thước truyền tối đa
    };

    spi_device_interface_config_t devcfg = {
        .command_bits = 0,           // Opcodes là 8 bit
        .address_bits = 0,          // Địa chỉ là 16 bit
        .dummy_bits = 0,
        .mode = 0,                   // SPI Mode 0
        .duty_cycle_pos = 0,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = 1000000,  // Tốc độ 10 MHz
        .spics_io_num = PIN_NUM_CS,  // Chân CS
        .queue_size = 7,             // Kích thước hàng đợi giao dịch
        // .pre_cb = spi_pre_transfer_callback,
        // .post_cb = spi_post_transfer_callback,
    };


    // gpio_set_pull_mode(PIN_NUM_MISO, GPIO_PULLUP_ONLY);
    // gpio_set_pull_mode(PIN_NUM_MOSI, GPIO_PULLUP_ONLY);
    // gpio_set_pull_mode(PIN_NUM_CLK, GPIO_PULLUP_ONLY);



    gpio_set_direction(PIN_NUM_WP, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_WP, 1);

    // // Khởi tạo BUS SPI
    // ret = spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    // ESP_ERROR_CHECK(ret);
    // // Thêm thiết bị F-RAM vào BUS
    // ret = spi_bus_add_device(SPI_HOST, &devcfg, &spiFram);
    // ESP_ERROR_CHECK(ret);
    // ESP_LOGI(FRAM_TAG, "SPI Master initialized successfully.");

     // Khởi tạo BUS SPI
    ret = spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE))
    {
        ESP_LOGE(FRAM_TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return false;
    }
    // Thêm thiết bị F-RAM vào BUS
    ret = spi_bus_add_device(SPI_HOST, &devcfg, &spiFram);
    if(ret != ESP_OK)
    {
        ESP_LOGE(FRAM_TAG, "spi_bus_add_device failed: %s", esp_err_to_name(ret));
        return false;
    }
    s_fram_initialized = true;
    ESP_LOGI(FRAM_TAG, "SPI Master initialized successfully.");
    return true;
}

bool Fram_Init(void)
{
    if(s_fram_initialized)
    {
        return true;
    }

    return spi_master_init();
}

void FRAM_Delete_All(void)
{
    if(!Fram_Init())
    {
        ESP_LOGE(FRAM_TAG, "FRAM init failed, cannot clear");
        return;
    }

    uint8_t buffer[256];
    memset(buffer, 0, sizeof(buffer));

    for(int dev = 0; dev < 10; dev++)
    {
        uint16_t addr = dev * FRAM_DEVICE_SIZE;

        for(uint16_t offset = 0; offset < FRAM_DEVICE_SIZE; offset += sizeof(buffer))
        {
            uint16_t chunk = (FRAM_DEVICE_SIZE - offset) < sizeof(buffer)
                                 ? (FRAM_DEVICE_SIZE - offset)
                                 : (uint16_t)sizeof(buffer);

            Fram_Write_Data(addr + offset, buffer, chunk);
        }

        printf("FRAM_DELETE Device %d cleared addr 0x%04X\n",
               dev,
               addr);
    }
    printf("FRAM_DELETE: Entire FRAM cleared");
}

void User_Spi_Transmit(uint8_t *data, uint16_t size, uint8_t *cs)
{
    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));

    // trans.cmd = 0x01;
    trans.cmd = 0xAA;
    trans.addr = 0xAAAA;
    trans.tx_buffer = data;
    trans.user = (void*)cs;
    trans.length = (size + 3) * 8;

    gpio_set_level(PIN_NUM_CS, 0);
    spi_device_transmit(spiFram, &trans);
    gpio_set_level(PIN_NUM_CS, 1);
}

void Fram_Write_Data(uint16_t address, uint8_t *data, uint16_t size) 
{
    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));

    Fram_Write_Enable();
    vTaskDelay(1);
    
    uint16_t size_to_send = size + 3;

    uint8_t *temp = (uint8_t *)malloc(size_to_send);
    memset((void*)temp, 0, size_to_send);

    uint8_t *ptr = temp;

    *ptr = OPCODE_WRITE;
    ptr++;

    *ptr = (address >> 8) & 0xFF;
    ptr++;

    *ptr = address & 0xFF;
    ptr++;

    memcpy((void*)ptr, (const void *)data, size);

    trans.cmd = 0;
    trans.addr = 0;

    trans.length = size_to_send * 8;
    trans.tx_buffer = temp;
    
    spi_device_transmit(spiFram, &trans);

    free(temp);

    ESP_LOGI(FRAM_TAG, "Wrote %s to address 0x%04X", data, address);
}

void Fram_Write_Enable(void)
{
    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));

    uint8_t data = OPCODE_WREN;
    trans.cmd = 0;
    trans.addr = 0;
    trans.length = 8;
    trans.tx_buffer = &data;

    spi_device_transmit(spiFram, &trans);
}


bool Fram_Read_Data(uint16_t address, uint8_t *data, uint16_t size) 
{
    esp_err_t ret;
    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));

    uint8_t *temp = (uint8_t *)calloc(size + 3, sizeof(uint8_t));
    if(temp == NULL)
    {
        ESP_LOGE("FRAM READ", "Do not enough RAM for temp read buffer");
        return false;
    }

    uint8_t command[3] = {0};
    command[0] = OPCODE_READ;         
    command[1] = (address >> 8) & 0xFF;    
    command[2] = address & 0xFF;          

    trans.cmd = 0;
    trans.addr = 0;

    trans.length = (size + 3)*8;       
    trans.tx_buffer = command;

    trans.rx_buffer = temp;
    trans.rxlength = (size + 3)*8;
    
    ret = spi_device_polling_transmit(spiFram, &trans);
    if(ret != ESP_OK)
    {
        ESP_LOGE("FRAM READ", "SPI polling transmit fail");
        free(temp);
        return false;
    }
    
    memcpy(data, (const void *)&temp[3], size);
    free(temp);
    
    return true;
}


void User_Fram_Task()
{
    uint8_t buf[56] = {0};
    // memcpy(buf, "Hello spi, add more data", strlen("Hello spi, add more data"));

    spi_master_init();


    // Fram_Write_Data(0x0000, buf, sizeof(buf));
    // memset(buf, 0, sizeof(buf));
    Fram_Read_Data(0x0000, buf, 20);
    ESP_LOGI("FRAM: ", "Data: %s", buf);
    // Fram_Write_Enable();

    while(1)
    {
        

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
