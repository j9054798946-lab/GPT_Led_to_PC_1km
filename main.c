/*
 * Version: 2018 - ФИНАЛЬНАЯ РАБОЧАЯ с АЦП
 * AT91SAM7S256 - 4? АЦП AD7680 + 4? ЦАП MCP4726 + управление с ПК
 */
#include "AT91SAM7S256.h"
#include <intrinsics.h>
#include <stdbool.h>
#include "mcp4726.h"

#define LED_PIN (1 << 7)

#define PIN_CS     (1 << 12)
#define PIN_DATA   (1 << 15)
#define PIN_SCLK   (1 << 25)

volatile unsigned int short_counter = 0;
volatile uint32_t g_tick_counter = 0;

// ========== Протокол команд ==========
#define CMD_PACKET_LEN      6
#define CMD_HEADER1         0xAA
#define CMD_HEADER2         0x55

#define CMD_TEST_SEQUENTIAL 0x01
#define CMD_ALIGN_CHANNELS  0x02
#define CMD_MEANDER         0x03
#define CMD_SET_DAC         0x04

// ========== Флаги управления ==========
volatile bool g_test_sequential_enabled = false;

// ========== Буфер приема команд ==========
static uint8_t rx_cmd_buffer[CMD_PACKET_LEN];
static volatile uint8_t rx_cmd_index = 0;

volatile unsigned int led_state = 0;

#define MCK 27752640UL

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
        case 0: data_pin = (1 << 15); break;  // PA15
        case 1: data_pin = (1 << 24); break;  // PA24
        case 2: data_pin = (1 << 10); break;  // PA10
        case 3: data_pin = (1 << 29); break;  // PA29
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
    
    // Отключить прерывания USART
    AT91C_BASE_US0->US_IDR = 0xFFFFFFFF;
    AT91C_BASE_US0->US_CR = AT91C_US_RSTSTA;
}

void usart0_putc(char c)
{
    while (!(AT91C_BASE_US0->US_CSR & AT91C_US_TXRDY));
    AT91C_BASE_US0->US_THR = c;
}

void usart0_puts(const char* str)
{
    while (*str) {
        usart0_putc(*str++);
    }
}

// ========== Обработка принятой команды ==========
void process_command(uint8_t *cmd_buf)
{
    if (cmd_buf[0] != CMD_HEADER1 || cmd_buf[1] != CMD_HEADER2) {
        return;
    }

    uint8_t cmd = cmd_buf[2];
    uint8_t data1 = cmd_buf[3];
    uint8_t data2 = cmd_buf[4];
    uint8_t checksum = cmd_buf[5];

    uint8_t calc_checksum = cmd ^ data1 ^ data2;
    if (checksum != calc_checksum) {
        return;
    }

    // Отправка Echo
    usart0_putc(0xEE);
    usart0_putc(cmd);
    usart0_putc(data1);
    usart0_putc(data2);

    // Обработка команд
    switch (cmd) {
        case CMD_TEST_SEQUENTIAL:
            g_test_sequential_enabled = (data1 == 0x01);
            break;

        case CMD_ALIGN_CHANNELS:
            // Будет реализовано позже
            break;

        case CMD_MEANDER:
            // Будет реализовано позже
            break;

        default:
            break;
    }
}

// ========== Прием команды по USART ==========
void check_usart_rx(void)
{
    uint32_t status = AT91C_BASE_US0->US_CSR;
    if (status & (AT91C_US_OVRE | AT91C_US_FRAME | AT91C_US_PARE)) {
        AT91C_BASE_US0->US_CR = AT91C_US_RSTSTA;
    }
    
    while (AT91C_BASE_US0->US_CSR & AT91C_US_RXRDY) {
        uint8_t received_byte = AT91C_BASE_US0->US_RHR & 0xFF;

        if (rx_cmd_index == 0) {
            if (received_byte == CMD_HEADER1) {
                rx_cmd_buffer[rx_cmd_index++] = received_byte;
            }
        }
        else if (rx_cmd_index == 1) {
            if (received_byte == CMD_HEADER2) {
                rx_cmd_buffer[rx_cmd_index++] = received_byte;
            } else {
                rx_cmd_index = 0;
            }
        }
        else {
            rx_cmd_buffer[rx_cmd_index++] = received_byte;

            if (rx_cmd_index >= CMD_PACKET_LEN) {
                process_command(rx_cmd_buffer);
                rx_cmd_index = 0;
            }
        }
    }
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

// ============ ПОСЛЕДОВАТЕЛЬНЫЙ ТЕСТ КАНАЛОВ ============
void test_dac_sequential_channels(void) 
{
    static uint16_t dac_value = 0;
    static uint8_t current_channel = 0;
    static uint32_t last_update = 0;
    
    if (check_timeout(&last_update, 2000)) {  // Каждую 1 секунду
        
        dac_value += 200;
        
        if (dac_value > 4095) {
            dac_value = 0;
            current_channel++;
            if (current_channel >= 4) {
                current_channel = 0;
            }
        }
        
        mcp4726_write_dac_channel(current_channel, dac_value);
    }
}

// ---------------- PIT ----------------
void PIT_Handler(void)
{
    volatile unsigned int dummy = AT91C_BASE_PITC->PITC_PIVR;
    (void)dummy;
    
    g_tick_counter++;
    
    // ========== ЧТЕНИЕ АЦП И ОТПРАВКА ПАКЕТА ==========
    /*unsigned short adc_values[4];
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
        usart0_putc(pkt[i]);*/
  // ========== ВРЕМЕННО: Отправка АЦП раз в 10 мс вместо 500 мкс ==========
    static uint16_t adc_divider = 0;
    adc_divider++;
    
    if (adc_divider >= 20) {  // 20 ? 500мкс = 10 мс
        adc_divider = 0;
        
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
    }
    // ========== LED мигание каждые 500 мс ==========
    short_counter++;
    if (short_counter >= 1000) {      // 1000 ? 500 мкс = 500 мс
        short_counter = 0;
        if (led_state) {
            AT91C_BASE_PIOA->PIO_SODR = LED_PIN;
            led_state = 0;
        } else {
            AT91C_BASE_PIOA->PIO_CODR = LED_PIN;  // можно здесь коммент. для не включения
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
    // Отключаем watchdog
    AT91C_BASE_WDTC->WDTC_WDMR = AT91C_WDTC_WDDIS;

    // Инициализация LED
    AT91C_BASE_PMC->PMC_PCER = (1 << AT91C_ID_PIOA);
    AT91C_BASE_PIOA->PIO_PER = LED_PIN;
    AT91C_BASE_PIOA->PIO_OER = LED_PIN;
    AT91C_BASE_PIOA->PIO_SODR = LED_PIN;  // OFF по умолчанию
 
    // Инициализация АЦП
    adc_init();
    
    // AIC: разрешаем SYS-прерывания (PIT)
    AT91C_BASE_AIC->AIC_IDCR = (1 << AT91C_ID_SYS);
    AT91C_BASE_AIC->AIC_ICCR = (1 << AT91C_ID_SYS);
    AT91C_BASE_AIC->AIC_IECR = (1 << AT91C_ID_SYS);

    // PIT на 500 мкс
    AT91C_BASE_PITC->PITC_PIMR = AT91C_PITC_PITEN
                               | AT91C_PITC_PITIEN
                               | 867;

    // Инициализация USART0
    usart0_init(256000);
    
    // Инициализация MCP4726 (все 4 канала)
    mcp4726_init_all();
    
    __enable_interrupt();

    while (1) {
        // Проверка приема команд с ПК
        check_usart_rx();
        
        // Выполнение последовательного теста по флагу
        if (g_test_sequential_enabled) {
            test_dac_sequential_channels();
        }
    }
}