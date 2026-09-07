#pragma once

#include "stdint.h"
#include "stdbool.h"

#define SPI_HOST                SPI3_HOST // Sử dụng SPI3_HOST (HSPI) để tránh đụng độ bus SPI2_HOST của LCD
// #define PIN_NUM_MISO 47
// #define PIN_NUM_MOSI 48
// #define PIN_NUM_CLK  45
// #define PIN_NUM_CS   12

#define PIN_NUM_MISO 16
#define PIN_NUM_MOSI 6
#define PIN_NUM_CLK  7
#define PIN_NUM_CS   15

// #define PIN_NUM_WP   14

// #define PIN_NUM_CS_2   21

#define OPCODE_WREN  0x06  // Write Enable
#define OPCODE_WRDI  0x04  // Write Disable
#define OPCODE_READ  0x03  // Read Memory
#define OPCODE_WRITE 0x02  // Write Memory
#define OPCODE_RDSR  0x05  // Read Status Register
#define OPCODE_WRSR  0x01  // Write Status Register



#define FRAM_LOG_MAGIC          0x4652414D  // Magic Key "FRAM"
#define FRAM_LOG_HEADER_ADDR    0x2000
#define FRAM_LOG_DATA_START     0x2018
#define FRAM_LOG_RECORD_SIZE    12
#define FRAM_LOG_MAX_RECORDS    2046

typedef struct __attribute__((packed)) {
    uint32_t timestamp;     // Unix Epoch time (seconds)
    uint16_t ph_x100;        // pH * 100 (0.00 to 14.00 -> 0 to 1400)
    int16_t  temp_x100;      // Water Temp * 100 (-10.00 to 60.00 -> -1000 to 6000)
    uint16_t do_x100;        // DO mg/L * 100 (0.00 to 20.00 -> 0 to 2000)
    uint8_t  flags;          // Bit 0: pH valid, Bit 1: DO valid, Bit 2..7: Reserved/Error
    uint8_t  reserved;       // Alignment byte (Total = 12 Bytes)
} EnvLogRecord_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;         // Magic Key (0x4652414D)
    uint16_t head_index;    // Write index (0 to 2045)
    uint16_t tail_index;    // Read index (0 to 2045)
    uint16_t record_count;  // Current number of valid records (0 to 2046)
    uint16_t synced_index;  // Next sync index for Azure (0 to 2045)
    uint16_t max_records;   // Maximum records (2046)
    uint16_t record_size;   // Kích thước 1 bản ghi (12 Bytes)
    uint8_t  reserved[8];   // Đệm cho đủ 24 Bytes header (0x2000 to 0x2017)
} Fram_Log_Header_t;


void FRAM_Delete_All(void);

void User_Fram_Task();

void Fram_Write_Enable(void);

void Fram_Write_Data(uint16_t address, uint8_t *data, uint16_t size);

bool Fram_Read_Data(uint16_t address, uint8_t *data, uint16_t size);

bool Fram_Init(void);

// === FRAM Environment Logging APIs ===
bool Fram_Log_Init(void);
bool Fram_Log_Write_Record(float ph, float temp, float do_mg_l, uint8_t flags);
bool Fram_Log_Read_Record(uint16_t relative_index, EnvLogRecord_t *record_out);
bool Fram_Log_Read_Latest(EnvLogRecord_t *record_out);
uint16_t Fram_Log_Get_Count(void);
void Fram_Log_Clear_All(void);

// === Offline Telemetry Sync APIs ===
uint16_t Fram_Log_Get_Unsynced_Count(void);
bool Fram_Log_Get_Unsynced_Batch(EnvLogRecord_t *records_out, uint16_t max_records, uint16_t *count_out);
bool Fram_Log_Commit_Synced_Count(uint16_t count);

