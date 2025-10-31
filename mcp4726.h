/**
 * @file    mcp4726.h
 * @brief   Драйвер для 4-х каналов 12-bit ЦАП MCP4726 через программный I2C
 *          для микроконтроллера AT91SAM7S256
 * @note    Общая линия SCL, индивидуальные SDA для каждого канала
 */

#ifndef MCP4726_H
#define MCP4726_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ===================== Конфигурация пинов ===================== */
#define MCP4726_SCL_PIN        14u     // PA14 - общая линия тактирования для всех каналов

// Индивидуальные линии данных для каждого канала
#define MCP4726_SDA1_PIN       31u     // PA31 - канал 0
#define MCP4726_SDA2_PIN       13u     // PA13 - канал 1
#define MCP4726_SDA3_PIN       11u     // PA11 - канал 2
#define MCP4726_SDA4_PIN       9u      // PA9  - канал 3

/* ===================== Номера каналов ===================== */
#define MCP4726_CHANNEL_0      0u
#define MCP4726_CHANNEL_1      1u
#define MCP4726_CHANNEL_2      2u
#define MCP4726_CHANNEL_3      3u
#define MCP4726_CHANNEL_COUNT  4u

/* ===================== Конфигурация устройства ===================== */
#define MCP4726_DEFAULT_ADDR   0x60u   // I2C адрес (одинаковый для всех)

/* ===================== Константы MCP4726 ===================== */
#define MCP4726_MAX_VALUE      0x0FFFu // Максимальное 12-битное значение

/* Режимы записи */
typedef enum {
    MCP4726_MODE_FAST_WRITE   = 0x00,  // Быстрая запись
    MCP4726_MODE_WRITE_DAC    = 0x40,  // Запись в DAC register
    MCP4726_MODE_WRITE_EEPROM = 0x60   // Запись в DAC и EEPROM
} mcp4726_write_mode_t;

/* Режимы питания */
typedef enum {
    MCP4726_PD_NORMAL    = 0x00,
    MCP4726_PD_1K        = 0x01,
    MCP4726_PD_100K      = 0x02,
    MCP4726_PD_500K      = 0x03
} mcp4726_powerdown_t;

/* Опорное напряжение */
typedef enum {
    MCP4726_VREF_VDD     = 0x00,
    MCP4726_VREF_INTERNAL = 0x01
} mcp4726_vref_t;

/* Усиление */
typedef enum {
    MCP4726_GAIN_1X      = 0x00,
    MCP4726_GAIN_2X      = 0x01
} mcp4726_gain_t;

/* Структура конфигурации MCP4726 */
typedef struct {
    uint8_t             i2c_addr;
    mcp4726_vref_t      vref;
    mcp4726_powerdown_t powerdown;
    mcp4726_gain_t      gain;
} mcp4726_config_t;

/* ===================== Публичные функции ===================== */

/**
 * @brief Инициализация всех 4-х каналов DAC
 * @note  Настраивает GPIO для всех SDA линий и общей SCL
 */
void mcp4726_init_all(void);

/**
 * @brief Запись 12-битного значения в выбранный канал DAC
 * @param channel Номер канала (0-3)
 * @param value12 Значение от 0 до 4095
 * @return true если успешно, false при ошибке
 */
bool mcp4726_write_dac_channel(uint8_t channel, uint16_t value12);

/**
 * @brief Быстрая запись в выбранный канал
 * @param channel Номер канала (0-3)
 * @param value12 Значение от 0 до 4095
 * @return true если успешно, false при ошибке
 */
bool mcp4726_fast_write_channel(uint8_t channel, uint16_t value12);

/**
 * @brief Установка всех каналов в 0
 * @return true если все успешно
 */
bool mcp4726_set_all_zero(void);

/**
 * @brief Установка всех каналов на максимум
 * @return true если все успешно
 */
bool mcp4726_set_all_max(void);

/**
 * @brief Установка всех каналов на одно значение
 * @param value12 Значение от 0 до 4095
 * @return true если все успешно
 */
bool mcp4726_set_all_value(uint16_t value12);

/**
 * @brief Установка выбранного канала в 0
 * @param channel Номер канала (0-3)
 * @return true если успешно
 */
bool mcp4726_set_channel_zero(uint8_t channel);

/**
 * @brief Установка выбранного канала на максимум
 * @param channel Номер канала (0-3)
 * @return true если успешно
 */
bool mcp4726_set_channel_max(uint8_t channel);

/**
 * @brief Настройка задержки I2C
 * @param delay_loops Количество циклов задержки
 */
void mcp4726_set_i2c_delay(uint16_t delay_loops);

/* ===================== Обратная совместимость ===================== */
// Старые функции теперь работают с каналом 0
#define mcp4726_init()           mcp4726_init_all()
#define mcp4726_write_dac(val)   mcp4726_write_dac_channel(0, val)
#define mcp4726_set_zero()       mcp4726_set_channel_zero(0)

#endif /* MCP4726_H */