/**
 * @file    mcp4726.c
 * @brief   Реализация драйвера MCP4726 для 4-х каналов
 */

#include "mcp4726.h"
#include <intrinsics.h>

/* ===================== AT91SAM7S256: Регистры ===================== */
#define PMC_BASE        0xFFFFFC00u
#define PMC_PCER       (*(volatile uint32_t*)(PMC_BASE + 0x10))

#define PIOA_BASE       0xFFFFF400u
#define PIOA_PER       (*(volatile uint32_t*)(PIOA_BASE + 0x00))
#define PIOA_PDR       (*(volatile uint32_t*)(PIOA_BASE + 0x04))
#define PIOA_OER       (*(volatile uint32_t*)(PIOA_BASE + 0x10))
#define PIOA_ODR       (*(volatile uint32_t*)(PIOA_BASE + 0x14))
#define PIOA_SODR      (*(volatile uint32_t*)(PIOA_BASE + 0x30))
#define PIOA_CODR      (*(volatile uint32_t*)(PIOA_BASE + 0x34))
#define PIOA_PDSR      (*(volatile uint32_t*)(PIOA_BASE + 0x3C))
#define PIOA_MDER      (*(volatile uint32_t*)(PIOA_BASE + 0x50))
#define PIOA_PUER      (*(volatile uint32_t*)(PIOA_BASE + 0x64))

#define ID_PIOA        2u

/* ===================== Маски пинов ===================== */
#define SCL_MASK       (1u << MCP4726_SCL_PIN)
#define SDA1_MASK      (1u << MCP4726_SDA1_PIN)
#define SDA2_MASK      (1u << MCP4726_SDA2_PIN)
#define SDA3_MASK      (1u << MCP4726_SDA3_PIN)
#define SDA4_MASK      (1u << MCP4726_SDA4_PIN)

#define SDA_ALL_MASK   (SDA1_MASK | SDA2_MASK | SDA3_MASK | SDA4_MASK)

/* ===================== Внутренние переменные ===================== */
static uint8_t  g_mcp4726_addr = MCP4726_DEFAULT_ADDR;
static uint16_t g_i2c_delay_loops = 48;  // Для MCK ~27 МГц
static bool     g_initialized = false;
static uint32_t g_current_sda_mask = SDA1_MASK;  // Текущая активная линия SDA

/* Таблица масок для быстрого доступа */
static const uint32_t g_sda_masks[MCP4726_CHANNEL_COUNT] = {
    SDA1_MASK,  // Канал 0
    SDA2_MASK,  // Канал 1
    SDA3_MASK,  // Канал 2
    SDA4_MASK   // Канал 3
};

/* ===================== Программная задержка для I2C ===================== */
static void i2c_delay(void) {
    for (volatile uint16_t i = 0; i < g_i2c_delay_loops; i++) {
        __no_operation();
    }
}

/* ===================== Низкоуровневые операции I2C ===================== */
static inline void SDA_HIGH(void) { 
    PIOA_SODR = g_current_sda_mask; 
}

static inline void SDA_LOW(void) { 
    PIOA_CODR = g_current_sda_mask; 
}

static inline void SCL_HIGH(void) {
    PIOA_SODR = SCL_MASK;
    // Clock stretching support
    for (volatile uint16_t t = 0; t < 1000; t++) {
        if (PIOA_PDSR & SCL_MASK) break;
    }
}

static inline void SCL_LOW(void) { 
    PIOA_CODR = SCL_MASK; 
}

static inline bool SDA_READ(void) { 
    return (PIOA_PDSR & g_current_sda_mask) != 0; 
}

/* I2C START condition */
static void i2c_start(void) {
    SDA_HIGH();
    SCL_HIGH();
    i2c_delay();
    SDA_LOW();
    i2c_delay();
    SCL_LOW();
}

/* I2C STOP condition */
static void i2c_stop(void) {
    SDA_LOW();
    i2c_delay();
    SCL_HIGH();
    i2c_delay();
    SDA_HIGH();
    i2c_delay();
}

/* Запись одного бита */
static inline void i2c_write_bit(bool bit) {
    if (bit) {
        SDA_HIGH();
    } else {
        SDA_LOW();
    }
    i2c_delay();
    SCL_HIGH();
    i2c_delay();
    SCL_LOW();
}

/* Чтение ACK */
static bool i2c_read_ack(void) {
    SDA_HIGH();
    i2c_delay();
    SCL_HIGH();
    i2c_delay();
    bool nack = SDA_READ();
    SCL_LOW();
    return !nack;
}

/* Запись байта */
static bool i2c_write_byte(uint8_t data) {
    for (uint8_t i = 0; i < 8; i++) {
        i2c_write_bit((data & 0x80) != 0);
        data <<= 1;
    }
    return i2c_read_ack();
}

/* ===================== Выбор канала ===================== */
static bool select_channel(uint8_t channel) {
    if (channel >= MCP4726_CHANNEL_COUNT) {
        return false;
    }
    g_current_sda_mask = g_sda_masks[channel];
    return true;
}

/* ===================== Инициализация GPIO ===================== */
static void soft_i2c_gpio_init(void) {
    // Включить тактирование PIOA
    PMC_PCER = (1u << ID_PIOA);

    // Передать управление PIO контроллеру для всех линий
    PIOA_PER = SCL_MASK | SDA_ALL_MASK;

    // Включить режим open-drain для всех линий
    PIOA_MDER = SCL_MASK | SDA_ALL_MASK;

    // Включить внутренние подтяжки
    PIOA_PUER = SCL_MASK | SDA_ALL_MASK;

    // Включить режим output
    PIOA_OER = SCL_MASK | SDA_ALL_MASK;

    // Установить все линии в высокое состояние
    PIOA_SODR = SCL_MASK | SDA_ALL_MASK;

    i2c_delay();
}

/* ===================== Публичные функции ===================== */

void mcp4726_init_all(void) {
    if (!g_initialized) {
        soft_i2c_gpio_init();
        g_mcp4726_addr = MCP4726_DEFAULT_ADDR;
        g_initialized = true;
        
        // Установить все каналы в 0
        mcp4726_set_all_zero();
    }
}

bool mcp4726_write_dac_channel(uint8_t channel, uint16_t value12) {
    if (!select_channel(channel)) {
        return false;
    }
    
    if (value12 > MCP4726_MAX_VALUE) {
        value12 = MCP4726_MAX_VALUE;
    }

    uint8_t addr_w = (g_mcp4726_addr << 1) | 0;
    uint8_t cmd    = MCP4726_MODE_WRITE_DAC;
    uint8_t data_h = (value12 >> 4) & 0xFF;
    uint8_t data_l = (value12 & 0x0F) << 4;

    i2c_start();
    bool ok = i2c_write_byte(addr_w);
    ok &= i2c_write_byte(cmd);
    ok &= i2c_write_byte(data_h);
    ok &= i2c_write_byte(data_l);
    i2c_stop();

    return ok;
}

bool mcp4726_fast_write_channel(uint8_t channel, uint16_t value12) {
    if (!select_channel(channel)) {
        return false;
    }
    
    if (value12 > MCP4726_MAX_VALUE) {
        value12 = MCP4726_MAX_VALUE;
    }

    uint8_t addr_w = (g_mcp4726_addr << 1) | 0;
    uint8_t data_h = (value12 >> 8) & 0x0F;
    uint8_t data_l = value12 & 0xFF;

    i2c_start();
    bool ok = i2c_write_byte(addr_w);
    ok &= i2c_write_byte(data_h);
    ok &= i2c_write_byte(data_l);
    i2c_stop();

    return ok;
}

bool mcp4726_set_all_zero(void) {
    bool ok = true;
    for (uint8_t ch = 0; ch < MCP4726_CHANNEL_COUNT; ch++) {
        ok &= mcp4726_write_dac_channel(ch, 0x000);
    }
    return ok;
}

bool mcp4726_set_all_max(void) {
    bool ok = true;
    for (uint8_t ch = 0; ch < MCP4726_CHANNEL_COUNT; ch++) {
        ok &= mcp4726_write_dac_channel(ch, MCP4726_MAX_VALUE);
    }
    return ok;
}

bool mcp4726_set_all_value(uint16_t value12) {
    bool ok = true;
    for (uint8_t ch = 0; ch < MCP4726_CHANNEL_COUNT; ch++) {
        ok &= mcp4726_write_dac_channel(ch, value12);
    }
    return ok;
}

bool mcp4726_set_channel_zero(uint8_t channel) {
    return mcp4726_write_dac_channel(channel, 0x000);
}

bool mcp4726_set_channel_max(uint8_t channel) {
    return mcp4726_write_dac_channel(channel, MCP4726_MAX_VALUE);
}

void mcp4726_set_i2c_delay(uint16_t delay_loops) {
    g_i2c_delay_loops = delay_loops;
}