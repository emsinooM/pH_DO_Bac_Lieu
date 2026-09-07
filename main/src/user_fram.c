#include "user_fram.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "string.h"
#include "user_ouput.h"
#include "user_azure.h"
#include "user_system.h"
#include "ph_temp.h"
#include "time.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define MAX_DEVICE          10
#define DEVICE_NAME_LEN     16
#define FRAM_START   0x0000
#define FRAM_LOCK()    do { if (s_fram_mutex) xSemaphoreTakeRecursive(s_fram_mutex, portMAX_DELAY); } while(0)
#define FRAM_UNLOCK()  do { if (s_fram_mutex) xSemaphoreGiveRecursive(s_fram_mutex); } while(0)

static bool s_fram_initialized = false;
static SemaphoreHandle_t s_fram_mutex = NULL;

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



    // PIN_NUM_WP is tied to 3V3 by hardware default

    // // Khởi tạo BUS SPI
    // ret = spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    // ESP_ERROR_CHECK(ret);
    // // Thêm thiết bị F-RAM vào BUS
    // ret = spi_bus_add_device(SPI_HOST, &devcfg, &spiFram);
    // ESP_ERROR_CHECK(ret);
    // ESP_LOGI(FRAM_TAG, "SPI Master initialized successfully.");

     // Khởi tạo BUS SPI (Không dùng DMA - SPI_DMA_DISABLED để an toàn 100% với biến Stack và truyền FIFO nhanh nhất)
    ret = spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_DISABLED);
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
    if (s_fram_mutex == NULL) {
        s_fram_mutex = xSemaphoreCreateRecursiveMutex();
        if (s_fram_mutex == NULL) {
            ESP_LOGE(FRAM_TAG, "Khong the khoi tao FRAM Recursive Mutex");
            return false;
        }
    }

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
    if (!Fram_Init() || data == NULL || size == 0) return;
    FRAM_LOCK();

    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));

    Fram_Write_Enable();
    
    uint16_t size_to_send = size + 3;
    uint8_t stack_buf[32];
    uint8_t *temp = stack_buf;
    bool heap_used = false;

    if (size_to_send > sizeof(stack_buf)) {
        temp = (uint8_t *)malloc(size_to_send);
        if (temp == NULL) {
            ESP_LOGE(FRAM_TAG, "Malloc that bai trong Fram_Write_Data");
            FRAM_UNLOCK();
            return;
        }
        heap_used = true;
    }

    temp[0] = OPCODE_WRITE;
    temp[1] = (address >> 8) & 0xFF;
    temp[2] = address & 0xFF;
    memcpy(&temp[3], data, size);

    trans.cmd = 0;
    trans.addr = 0;
    trans.length = size_to_send * 8;
    trans.tx_buffer = temp;
    
    spi_device_transmit(spiFram, &trans);

    if (heap_used) {
        free(temp);
    }

    FRAM_UNLOCK();

    ESP_LOGI(FRAM_TAG, "Wrote %u bytes to address 0x%04X", size, address);
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
    if (!Fram_Init() || data == NULL || size == 0) return false;
    FRAM_LOCK();

    esp_err_t ret;
    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));

    uint16_t total_size = size + 3;
    uint8_t stack_rx[32];
    uint8_t *temp = stack_rx;
    bool heap_used = false;

    if (total_size > sizeof(stack_rx)) {
        temp = (uint8_t *)calloc(total_size, sizeof(uint8_t));
        if (temp == NULL) {
            ESP_LOGE("FRAM READ", "Do not enough RAM for temp read buffer");
            FRAM_UNLOCK();
            return false;
        }
        heap_used = true;
    } else {
        memset(stack_rx, 0, sizeof(stack_rx));
    }

    uint8_t command[3] = {0};
    command[0] = OPCODE_READ;         
    command[1] = (address >> 8) & 0xFF;    
    command[2] = address & 0xFF;          

    trans.cmd = 0;
    trans.addr = 0;
    trans.length = total_size * 8;       
    trans.tx_buffer = command;
    trans.rx_buffer = temp;
    trans.rxlength = total_size * 8;
    
    ret = spi_device_polling_transmit(spiFram, &trans);
    if (ret != ESP_OK) {
        ESP_LOGE("FRAM READ", "SPI polling transmit fail: %s", esp_err_to_name(ret));
        if (heap_used) free(temp);
        FRAM_UNLOCK();
        return false;
    }
    
    memcpy(data, (const void *)&temp[3], size);
    if (heap_used) free(temp);

    FRAM_UNLOCK();
    return true;
}



// === FRAM Environment Logging Implementation ===

bool Fram_Log_Init(void)
{
    if (!Fram_Init()) {
        ESP_LOGE(FRAM_TAG, "Fram_Init failed during Fram_Log_Init");
        return false;
    }

    Fram_Log_Header_t header;
    memset(&header, 0, sizeof(header));

    if (!Fram_Read_Data(FRAM_LOG_HEADER_ADDR, (uint8_t *)&header, sizeof(header))) {
        ESP_LOGE(FRAM_TAG, "Failed to read FRAM Log Header");
        return false;
    }

    if (header.magic != FRAM_LOG_MAGIC ||
        header.max_records != FRAM_LOG_MAX_RECORDS ||
        header.record_size != FRAM_LOG_RECORD_SIZE) {
        
        ESP_LOGW(FRAM_TAG, "Invalid/Uninitialized FRAM Log Header. Initializing new Header...");
        memset(&header, 0, sizeof(header));
        header.magic = FRAM_LOG_MAGIC;
        header.head_index = 0;
        header.tail_index = 0;
        header.record_count = 0;
        header.synced_index = 0;
        header.max_records = FRAM_LOG_MAX_RECORDS;
        header.record_size = FRAM_LOG_RECORD_SIZE;

        Fram_Write_Data(FRAM_LOG_HEADER_ADDR, (uint8_t *)&header, sizeof(header));
        ESP_LOGI(FRAM_TAG, "FRAM Environment Log Header initialized successfully.");
    } else {
        if (header.synced_index >= FRAM_LOG_MAX_RECORDS) {
            header.synced_index = header.tail_index;
            Fram_Write_Data(FRAM_LOG_HEADER_ADDR, (uint8_t *)&header, sizeof(header));
        }
        ESP_LOGI(FRAM_TAG, "FRAM Log Header valid: count=%u, head=%u, tail=%u, synced=%u",
                 header.record_count, header.head_index, header.tail_index, header.synced_index);
    }

    return true;
}

bool Fram_Log_Write_Record(float ph, float temp, float do_mg_l, uint8_t flags)
{
    if (!Fram_Init()) {
        return false;
    }

    FRAM_LOCK(); // 🔒 Khoa toan bo chu trinh Read-Modify-Write

    Fram_Log_Header_t header;
    if (!Fram_Read_Data(FRAM_LOG_HEADER_ADDR, (uint8_t *)&header, sizeof(header))) {
        ESP_LOGE(FRAM_TAG, "Write Record failed: cannot read header");
        return false;
    }

    if (header.magic != FRAM_LOG_MAGIC) {
        if (!Fram_Log_Init()) {
            FRAM_UNLOCK();
            return false;   
        }
        Fram_Read_Data(FRAM_LOG_HEADER_ADDR, (uint8_t *)&header, sizeof(header));
    }

    time_t cur_time = time(NULL);
    if (cur_time < 1700000000) {
        ESP_LOGW(FRAM_TAG, "Chua dong bo thoi gian thuc (time < 2024), bo qua ghi FRAM.");
        FRAM_UNLOCK();
        return false;
    }

    // Đóng gói dữ liệu (float -> int x100)
    EnvLogRecord_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.timestamp = (uint32_t)cur_time;
    rec.ph_x100 = (uint16_t)(ph * 100.0f + 0.5f);
    
    if (temp >= 0) {
        rec.temp_x100 = (int16_t)(temp * 100.0f + 0.5f);
    } else {
        rec.temp_x100 = (int16_t)(temp * 100.0f - 0.5f);
    }
    
    rec.do_x100 = (uint16_t)(do_mg_l * 100.0f + 0.5f);
    rec.flags = flags;
    rec.reserved = 0;

    // Tính địa chỉ ô nhớ slot hiện tại
    uint16_t write_addr = FRAM_LOG_DATA_START + (header.head_index * FRAM_LOG_RECORD_SIZE);

    Fram_Write_Data(write_addr, (uint8_t *)&rec, sizeof(rec));

    uint16_t written_slot = header.head_index;
    bool was_fully_synced = (header.synced_index == written_slot);

    // Tăng con trỏ head và quay vòng phần mềm (Software Wrap)
    header.head_index = (header.head_index + 1) % FRAM_LOG_MAX_RECORDS;

    if (header.record_count < FRAM_LOG_MAX_RECORDS) {
        header.record_count++;
    } else {
        // Đã đầy bộ nhớ: Đẩy con trỏ tail_index để loại bỏ bản ghi cũ nhất
        header.tail_index = (header.tail_index + 1) % FRAM_LOG_MAX_RECORDS;
        if (header.synced_index == written_slot) {
            header.synced_index = header.tail_index;
        }
    }

    // Kiểm tra trạng thái kết nối Internet & Azure
    bool is_online = Is_System_Internet_Connected() && IoTHubHandle.isAzureInitialized && !IoTHubHandle.isNeedReinit && !bIsOtaActivated;

    // Nếu đang Online và trước đó không bị tồn đọng bản ghi offline:
    // Tự động đánh dấu bản ghi này đã được đồng bộ trực tuyến (không cần push lại qua Code 510)
    if (is_online && was_fully_synced) {
        header.synced_index = header.head_index;
    }

    // Ghi đè cập nhật Header lại vào FRAM
    Fram_Write_Data(FRAM_LOG_HEADER_ADDR, (uint8_t *)&header, sizeof(header));

    uint16_t unsynced = 0;
    if (header.head_index >= header.synced_index) {
        unsynced = header.head_index - header.synced_index;
    } else {
        unsynced = FRAM_LOG_MAX_RECORDS - header.synced_index + header.head_index;
    }
    if (unsynced > header.record_count) unsynced = header.record_count;

    FRAM_UNLOCK();

    ESP_LOGI(FRAM_TAG, "Log written at slot %u (addr 0x%04X): pH=%.2f, Temp=%.2f, DO=%.2f | [Head: %u, Synced: %u, Unsynced: %u] (%s)",
             written_slot, write_addr, ph, temp, do_mg_l, header.head_index, header.synced_index, unsynced,
             (unsynced == 0) ? "ONLINE - Auto Synced" : "OFFLINE - Buffered");

    return true;
}

bool Fram_Log_Read_Record(uint16_t relative_index, EnvLogRecord_t *record_out)
{
    if (record_out == NULL || !Fram_Init()) return false;

    FRAM_LOCK();

    Fram_Log_Header_t header;
    if (!Fram_Read_Data(FRAM_LOG_HEADER_ADDR, (uint8_t *)&header, sizeof(header))) {
        FRAM_UNLOCK();
        return false;
    }

    if (header.magic != FRAM_LOG_MAGIC || relative_index >= header.record_count) {
        FRAM_UNLOCK();
        return false;
    }

    // Tính slot index: relative_index = 0 là bản ghi cũ nhất (tail_index)
    uint16_t slot_index = (header.tail_index + relative_index) % FRAM_LOG_MAX_RECORDS;
    uint16_t read_addr = FRAM_LOG_DATA_START + (slot_index * FRAM_LOG_RECORD_SIZE);

    bool ok = Fram_Read_Data(read_addr, (uint8_t *)record_out, sizeof(EnvLogRecord_t));

    FRAM_UNLOCK();
    return ok;
}

bool Fram_Log_Read_Latest(EnvLogRecord_t *record_out)
{
    if (record_out == NULL || !Fram_Init()) return false;

    Fram_Log_Header_t header;
    if (!Fram_Read_Data(FRAM_LOG_HEADER_ADDR, (uint8_t *)&header, sizeof(header))) {
        return false;
    }

    if (header.magic != FRAM_LOG_MAGIC || header.record_count == 0) {
        return false;
    }

    return Fram_Log_Read_Record(header.record_count - 1, record_out);
}

uint16_t Fram_Log_Get_Count(void)
{
    if (!Fram_Init()) return 0;

    Fram_Log_Header_t header;
    if (!Fram_Read_Data(FRAM_LOG_HEADER_ADDR, (uint8_t *)&header, sizeof(header))) {
        return 0;
    }

    if (header.magic != FRAM_LOG_MAGIC) return 0;
    return header.record_count;
}

uint16_t Fram_Log_Get_Unsynced_Count(void)
{
    if (!Fram_Init()) return 0;

    Fram_Log_Header_t header;
    if (!Fram_Read_Data(FRAM_LOG_HEADER_ADDR, (uint8_t *)&header, sizeof(header))) {
        return 0;
    }

    if (header.magic != FRAM_LOG_MAGIC || header.record_count == 0) {
        return 0;
    }

    uint16_t unsynced = 0;
    if (header.head_index >= header.synced_index) {
        unsynced = header.head_index - header.synced_index;
    } else {
        unsynced = FRAM_LOG_MAX_RECORDS - header.synced_index + header.head_index;
    }

    if (unsynced > header.record_count) {
        unsynced = header.record_count;
    }

    return unsynced;
}

bool Fram_Log_Get_Unsynced_Batch(EnvLogRecord_t *records_out, uint16_t max_records, uint16_t *count_out)
{
    if (records_out == NULL || count_out == NULL || max_records == 0 || !Fram_Init()) {
        if (count_out) *count_out = 0;
        return false;
    }

    *count_out = 0;

    FRAM_LOCK();
    uint16_t unsynced = Fram_Log_Get_Unsynced_Count();
    if (unsynced == 0) {
        FRAM_UNLOCK();
        return true;
    }

    Fram_Log_Header_t header;
    if (!Fram_Read_Data(FRAM_LOG_HEADER_ADDR, (uint8_t *)&header, sizeof(header))) {
        FRAM_UNLOCK();
        return false;
    }

    uint16_t to_read = (unsynced > max_records) ? max_records : unsynced;

    for (uint16_t i = 0; i < to_read; i++) {
        uint16_t slot = (header.synced_index + i) % FRAM_LOG_MAX_RECORDS;
        uint16_t addr = FRAM_LOG_DATA_START + (slot * FRAM_LOG_RECORD_SIZE);
        if (!Fram_Read_Data(addr, (uint8_t *)&records_out[i], sizeof(EnvLogRecord_t))) {
            ESP_LOGE(FRAM_TAG, "Failed to read record at slot %u", slot);
            break;
        }
        (*count_out)++;
    }

    FRAM_UNLOCK();
    return true;
}

bool Fram_Log_Commit_Synced_Count(uint16_t count)
{
    if (count == 0 || !Fram_Init()) return true;

    FRAM_LOCK();

    Fram_Log_Header_t header;
    if (!Fram_Read_Data(FRAM_LOG_HEADER_ADDR, (uint8_t *)&header, sizeof(header))) {
        FRAM_UNLOCK();
        return false;
    }

    if (header.magic != FRAM_LOG_MAGIC){
        FRAM_UNLOCK();
        return false;
    } 

    header.synced_index = (header.synced_index + count) % FRAM_LOG_MAX_RECORDS;
    Fram_Write_Data(FRAM_LOG_HEADER_ADDR, (uint8_t *)&header, sizeof(header));

    FRAM_UNLOCK();
    
    ESP_LOGI(FRAM_TAG, "Committed %u synced records. New synced_index=%u", count, header.synced_index);
    return true;
}

void Fram_Log_Clear_All(void)
{
    if (!Fram_Init()) return;

    FRAM_LOCK();

    Fram_Log_Header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = FRAM_LOG_MAGIC;
    header.head_index = 0;
    header.tail_index = 0;
    header.record_count = 0;
    header.synced_index = 0;
    header.max_records = FRAM_LOG_MAX_RECORDS;
    header.record_size = FRAM_LOG_RECORD_SIZE;

    Fram_Write_Data(FRAM_LOG_HEADER_ADDR, (uint8_t *)&header, sizeof(header));
    
    FRAM_UNLOCK();

    ESP_LOGI(FRAM_TAG, "FRAM Environment Log cleared successfully.");
}

void User_Fram_Task()
{
    Fram_Log_Init();

    // Chờ 10 giây ban đầu cho cảm biến ổn định và đồng bộ thời gian nếu có
    vTaskDelay(pdMS_TO_TICKS(10000));

    while (1)
    {
        time_t cur_time = time(NULL);
        if (cur_time >= 1700000000)
        {
            PH_Temp_Sensor_Status_t status = Get_Sensor_Status();

            uint8_t flags = 0;
            if (status.is_calibrated) flags |= (1 << 0);
            if (status.do_valid)       flags |= (1 << 1);

            Fram_Log_Write_Record(status.ph, status.temperature, status.do_mg_l, flags);
        }
        else
        {
            ESP_LOGW(FRAM_TAG, "Chua co thoi gian thuc chuan (time < 2024), tam ngung ghi log FRAM...");
        }

        // Lưu định kỳ 1 phút (60,000 ms)
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}