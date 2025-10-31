#ifndef MY_DAC_H
#define MY_DAC_H

#include "AT91SAM7S256.h"

// --- пины ЦАП ---
#define PIN_SYNC    (1 << 14)   // общий SYNC  -> PA14
#define PIN_SCLK    (1 << 0)    // общий SCLK  -> PA0
#define PIN_SDIN1   (1 << 31)   // DATA0 -> PA31
#define PIN_SDIN2   (1 << 13)   // DATA1 -> PA13
#define PIN_SDIN3   (1 << 11)   // DATA2 -> PA11
#define PIN_SDIN4   (1 << 9)    // DATA3 -> PA9

// функция инициализации портов
void dac_init(void);

// функция записи в 4 ЦАП MCP4726 (12-бит)
void dac_write(unsigned short values[4]);

#endif