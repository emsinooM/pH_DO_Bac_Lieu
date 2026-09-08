/**
 * @file    large_font.h
 * @brief   Custom high-resolution font generated from Arial Bold for LCD ST7565/7
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t width;
    uint16_t bitmap[28];
} font_char_t;

typedef struct {
    uint8_t width;
    uint16_t bitmap[17];
} medium_font_char_t;

// Char mapping: '0'-'9' (0-9), '.' (10), 'p' (11), 'H' (12), 'C' (13), 'F' (14), '-' (15)
extern const font_char_t g_large_font[];
extern const medium_font_char_t g_medium_font[];

#ifdef __cplusplus
}
#endif
