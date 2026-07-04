#pragma once

#include "stdint.h"
#include "stdbool.h"

#define SPI_HOST                SPI2_HOST // Sử dụng VSPI host (SPI2_HOST tren ESP32-S3)
// #define PIN_NUM_MISO 47
// #define PIN_NUM_MOSI 48
// #define PIN_NUM_CLK  45
// #define PIN_NUM_CS   12

#define PIN_NUM_MISO 13
#define PIN_NUM_MOSI 11
#define PIN_NUM_CLK  12
#define PIN_NUM_CS   9

#define PIN_NUM_WP   14

// #define PIN_NUM_CS_2   21

#define OPCODE_WREN  0x06  // Write Enable
#define OPCODE_WRDI  0x04  // Write Disable
#define OPCODE_READ  0x03  // Read Memory
#define OPCODE_WRITE 0x02  // Write Memory
#define OPCODE_RDSR  0x05  // Read Status Register
#define OPCODE_WRSR  0x01  // Write Status Register


void FRAM_Delete_All(void);

void User_Fram_Task();

void Fram_Write_Enable(void);

void Fram_Write_Data(uint16_t address, uint8_t *data, uint16_t size);

bool Fram_Read_Data(uint16_t address, uint8_t *data, uint16_t size);

bool Fram_Init(void);
