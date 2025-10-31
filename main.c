/*
 * Version: 2014 + MCP4726 4-channel test
 * AT91SAM7S256 - LED blink + UART + 4x DAC
 */
#include "AT91SAM7S256.h"
#include <intrinsics.h>
#include "mcp4726.h"

#define LED_PIN (1 << 7)

#define PIN_CS     (1 << 12)
#define PIN_DATA   (1 << 15)
#define PIN_SCLK   (1 << 25)

volatile unsigned int short_counter = 0;
volatile uint32_t g_tick_counter = 0;

//---------------- ADC init -----------------
void adc_init(void)
{
    AT91C_BASE_PIOA->PIO_PER = PIN_CS | PIN_DATA | PIN_SCLK;
    AT91C_BASE_PIOA->PIO_OER = PIN_CS | PIN_SCLK;
    AT91C_BASE_PIOA->PIO_ODR = PIN_DATA;
    AT91C_BASE_PIOA->PIO_SODR = PIN_CS;
    AT91C_BASE_PIOA->PIO_CODR = PIN_SCLK;
}

//------------------- ADC read -----------------
unsigned short adc_read(unsigned int adc_num)
{
    unsigned short value = 0;
    unsigned int data_pin;
    int i;

    switch (adc_num) {
        case 0: data_pin = (1 << 15); break;
        case 1: data_pin = (1 << 24); break;
        case 2: data_pin = (1 << 10); break;
        case 3: data_pin = (1 << 29); break;
        default: data_pin = (1 << 15);
    }

    AT91C_BASE_PIOA->PIO_CODR = PIN_CS;

    for (i = 0; i < 4; i++) {
        AT91C_BASE_PIOA->PIO_CODR = PIN_SCLK;
        AT91C_BASE_PIOA->PIO_SODR = PIN_SCLK;
    }

    for (i = 0; i < 16; i++) {
        AT91C_BASE_PIOA->PIO_CODR = PIN_SCLK;
        AT91C_BASE_PIOA->PIO_SODR = PIN_SCLK;
        value <<= 1;
        if (AT91C_BASE_PIOA->PIO_PDSR & data_pin)
            value |= 1;
    }

    AT91C_BASE_PIOA->PIO_SODR = PIN_CS;
    return value;
}

volatile unsigned int led_state = 0;
volatile unsigned int pit_counter = 0;

#define MCK 27752640UL

// ---------------- USART0 ----------------
void usart0_init(unsigned int baud)
{
    unsigned int cd;
    AT91C_BASE_PMC->PMC_PCER = (1 << AT91C_ID_US0);
    AT91C_BASE_PIOA->PIO_PDR = (1<<5) | (1<<6);
    AT91C_BASE_PIOA->PIO_ASR = (1<<5) | (1<<6);
    AT91C_BASE_US0->US_CR = AT91C_US_RSTRX | AT91C_US_RSTTX
                          | AT91C_US_RXDIS | AT91C_US_TXDIS;
    AT91C_BASE_US0->US_MR = AT91C_US_USMODE_NORMAL
                          | AT91C_US_CLKS_CLOCK
                          | AT91C_US_CHRL_8_BITS
                          | AT91C_US_PAR_NONE
                          | AT91C_US_NBSTOP_1_BIT;
    cd = (MCK + (baud * 8)) / (16 * baud);
    AT91C_BASE_US0->US_BRGR = cd;
    AT91C_BASE_US0->US_CR = AT91C_US_TXEN | AT91C_US_RXEN;
}

void usart0_putc(char c)
{
    while (!(AT91C_BASE_US0->US_CSR & AT91C_US_TXRDY));
    AT91C_BASE_US0->US_THR = c;
}

// ============ ФУНКЦИЯ ПРОВЕРКИ ТАЙМАУТА ============
bool check_timeout(uint32_t *last_time, uint32_t ticks) 
{
    if ((g_tick_counter - *last_time) >= ticks) {
        *last_time = g_tick_counter;
        return true;
    }
    return false;
}

// ============ ТЕСТ "ПИЛА" ДЛЯ ОДНОГО КАНАЛА ============
void test_dac_ramp_single(void) 
{
    static uint16_t dac_value = 0;
    static uint32_t last_update = 0;
    
    if (check_timeout(&last_update, 2048)) {
        dac_value += 200;
        if (dac_value > 4095) {
            dac_value = 0;
        }
        mcp4726_write_dac_channel(0, dac_value);  // Только канал 0
    }
}

// ============ ТЕСТ "ПИЛА" ДЛЯ ВСЕХ 4-Х КАНАЛОВ ============
void test_dac_ramp_all_channels(void) 
{
    static uint16_t dac_values[4] = {0, 1000, 2000, 3000};  // Разные стартовые значения
    static uint32_t last_update = 0;
    
    // Обновление раз в секунду
    if (check_timeout(&last_update, 2000)) {
        
        // Обновить все 4 канала
        for (uint8_t ch = 0; ch < 4; ch++) {
            dac_values[ch] += 200;
            
            if (dac_values[ch] > 4095) {
                dac_values[ch] = 0;
            }
            
            // Записать в канал
            mcp4726_write_dac_channel(ch, dac_values[ch]);
        }
    }
}

// ============ АЛЬТЕРНАТИВА: Последовательный тест каналов ============
void test_dac_sequential_channels(void) 
{
    static uint16_t dac_value = 0;
    static uint8_t current_channel = 0;
    static uint32_t last_update = 0;
    
    // Обновление раз в секунду
    if (check_timeout(&last_update, 400)) {
        
        // Увеличить значение
        dac_value += 200;
        if (dac_value > 4095) {
            dac_value = 0;
            // При переполне переключиться на следующий канал
            current_channel++;
            if (current_channel >= 4) {
                current_channel = 0;
            }
        }
        
        // Записать в текущий канал
        mcp4726_write_dac_channel(current_channel, dac_value);
    }
}

// ---------------- PIT ----------------
void PIT_Handler(void)
{
    volatile unsigned int dummy = AT91C_BASE_PITC->PITC_PIVR;
    (void)dummy;
    
    g_tick_counter++;
    
    unsigned short adc_values[4];
    for (int n = 0; n < 4; n++)
        adc_values[n] = adc_read(n);
    
    unsigned char pkt[12];
    pkt[0] = led_state ? '1' : '0';
    for (int n = 0; n < 4; n++) {
        pkt[1 + 2*n] = (adc_values[n] >> 8) & 0xFF;
        pkt[2 + 2*n] = adc_values[n] & 0xFF;
    }
    pkt[9]  = 0xAA;
    pkt[10] = 0x55;
    pkt[11] = 0x00;
    
    for (int i = 0; i < 12; i++)
        usart0_putc(pkt[i]);

    short_counter++;
    if (short_counter >= 1000) {
        short_counter = 0;
        if (led_state) {
            AT91C_BASE_PIOA->PIO_SODR = LED_PIN;
            led_state = 0;
        } else {
            AT91C_BASE_PIOA->PIO_CODR = LED_PIN;
            led_state = 1;
        }
    }
}

__irq void IRQ_Handler(void)
{
    unsigned sr = AT91C_BASE_PITC->PITC_PISR;
    if (sr & AT91C_PITC_PITS) {
        PIT_Handler();
    }
    AT91C_BASE_AIC->AIC_EOICR = 0;
}

// ---------------- MAIN ----------------
int main(void)
{
    AT91C_BASE_WDTC->WDTC_WDMR = AT91C_WDTC_WDDIS;

    AT91C_BASE_PMC->PMC_PCER = (1 << AT91C_ID_PIOA);
    AT91C_BASE_PIOA->PIO_PER = LED_PIN;
    AT91C_BASE_PIOA->PIO_OER = LED_PIN;
    AT91C_BASE_PIOA->PIO_SODR = LED_PIN;
 
    adc_init();
    
    AT91C_BASE_AIC->AIC_IDCR = (1 << AT91C_ID_SYS);
    AT91C_BASE_AIC->AIC_ICCR = (1 << AT91C_ID_SYS);
    AT91C_BASE_AIC->AIC_IECR = (1 << AT91C_ID_SYS);

    AT91C_BASE_PITC->PITC_PIMR = AT91C_PITC_PITEN
                               | AT91C_PITC_PITIEN
                               | 867;

    usart0_init(256000);
    
    // Инициализация ВСЕХ 4-х каналов DAC
    mcp4726_init_all();
    
    // Опционально: установить все каналы в известное состояние
     mcp4726_set_all_zero();  // или
    // mcp4726_set_all_value(2048);  // середина диапазона
    
    __enable_interrupt();

    while (1) {
        // ВЫБЕРИТЕ ОДИН ИЗ ВАРИАНТОВ ТЕСТА:
        
        // Вариант 1: Все 4 канала меняются одновременно (с разными начальными значениями)
        //test_dac_ramp_all_channels();
        
        // Вариант 2: Только канал 0 (как раньше)
        // test_dac_ramp_single();
        
        // Вариант 3: Последовательный тест - каждый канал по очереди
         test_dac_sequential_channels();
    }
}