/**
 * @file    large_font.h
 * @brief   Custom high-resolution font generated from Arial Bold for LCD ST7565/7
 */

#ifndef LARGE_FONT_H
#define LARGE_FONT_H

#include <stdint.h>

typedef struct {
    uint8_t width;
    uint16_t bitmap[28];
} font_char_t;

// Char mapping: '0'-'9' (0-9), '.' (10), 'p' (11), 'H' (12), 'C' (13), 'F' (14)
extern const font_char_t g_large_font[];

#endif // LARGE_FONT_H
