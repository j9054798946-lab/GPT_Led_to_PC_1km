/*
 * Version: 2019 - С БАТЧИНГОМ (группировка пакетов)
 * Измерения каждые 500 мкс, отправка батчами каждые (уточнить) мс
 * USART0: PA5  (TXD0), PA6  (RXD0) - для приема команд с ПК
 * USART1: PA21 (TXD1), PA22 (RXD1) - для отправки данных АЦП
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

// Батчинг
#define BATCH_SIZE          50
#define ADC_CHANNELS        4

typedef struct {
    uint16_t adc[ADC_CHANNELS];
    uint8_t led;
} Measurement_t;

static Measurement_t batch_buffer[BATCH_SIZE];
static volatile uint8_t batch_index = 0;

// Протокол команд
#define CMD_MARKER  0xCC

// Защита от дублей команд
static uint8_t last_cmd = 0xFF;
static uint32_t last_cmd_time = 0;

// Флаги
volatile bool g_test_sequential_enabled = false;
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
// ========== USART1 init (для отправки данных АЦП) ==========
void usart1_init(unsigned int baud)
{
    unsigned int cd;
    
    // Включить тактирование USART1
    AT91C_BASE_PMC->PMC_PCER = (1 << AT91C_ID_US1);  // < ID_US1 вместо ID_US0
    
    // Назначить пины PA21=TXD1, PA22=RXD1 как Peripheral A
    AT91C_BASE_PIOA->PIO_PDR = (1<<21) | (1<<22);  // < PA21, PA22 вместо PA5, PA6
    AT91C_BASE_PIOA->PIO_ASR = (1<<21) | (1<<22);
    
    // Reset and disable TX/RX
    AT91C_BASE_US1->US_CR = AT91C_US_RSTRX | AT91C_US_RSTTX  // < US1 вместо US0
                          | AT91C_US_RXDIS | AT91C_US_TXDIS;
    
    // Mode: асинхрон, CLKS=MCK, 8N1
    AT91C_BASE_US1->US_MR = AT91C_US_USMODE_NORMAL
                          | AT91C_US_CLKS_CLOCK
                          | AT91C_US_CHRL_8_BITS
                          | AT91C_US_PAR_NONE
                          | AT91C_US_NBSTOP_1_BIT;
    
    // Расчет делителя скорости
    cd = (MCK + (baud * 8)) / (16 * baud);
    AT91C_BASE_US1->US_BRGR = cd;
    
    // Enable TX (RX не нужен для односторонней передачи)
    AT91C_BASE_US1->US_CR = AT91C_US_TXEN;  // < Только TXEN!
    
    // Отключить прерывания
    AT91C_BASE_US1->US_IDR = 0xFFFFFFFF;
    AT91C_BASE_US1->US_CR = AT91C_US_RSTSTA;
}
// ========== USART1 putc (отправка байта) ==========
void usart1_putc(char c)
{
    while (!(AT91C_BASE_US1->US_CSR & AT91C_US_TXRDY));  // < US1 вместо US0
    AT91C_BASE_US1->US_THR = c;
}

void usart1_puts(const char* str)
{
    while (*str) {
        usart1_putc(*str++);
    }
}

// ========== Отправка батча через USART1 ==========
void send_batch(void)
{
    // Заголовок батча
    usart1_putc(0xBB);  // < ИЗМЕНЕНО: usart1 вместо usart0
    usart1_putc(batch_index);
    
    // Отправка всех измерений
    for (uint8_t i = 0; i < batch_index; i++) {
        usart1_putc(batch_buffer[i].led ? '1' : '0');  // < usart1
        
        for (uint8_t ch = 0; ch < ADC_CHANNELS; ch++) {
            usart1_putc((batch_buffer[i].adc[ch] >> 8) & 0xFF);  // < usart1
            usart1_putc(batch_buffer[i].adc[ch] & 0xFF);          // < usart1
        }
    }
    
    // Маркер конца
    usart1_putc(0xCC);  // < usart1
    
    batch_index = 0;
}

// ========== Обработка принятой команды ==========
void process_command(uint8_t cmd, uint8_t arg)
{
    // Игнорировать дубликаты
    if (cmd == last_cmd && (g_tick_counter - last_cmd_time) < 200) {
        return;
    }
    
    last_cmd = cmd;
    last_cmd_time = g_tick_counter;
    
    // Echo отправляем обратно через USART0
    usart0_putc(0xEE);  // < Оставляем usart0!
    usart0_putc(cmd);
    
    switch (cmd) {
        case 0x01:
            g_test_sequential_enabled = true;
            break;
        case 0x02:
            g_test_sequential_enabled = false;
            break;
        default:
            break;
    }
}

// ========== ИСПРАВЛЕННАЯ версия check_usart_rx() ==========
void check_usart_rx(void)
{
    static uint8_t rx_state = 0;
    static uint8_t rx_cmd = 0;
    static uint8_t rx_arg = 0;  // < ДОБАВИТЬ для хранения аргумента
    
    uint32_t status = AT91C_BASE_US0->US_CSR;
    if (status & (AT91C_US_OVRE | AT91C_US_FRAME | AT91C_US_PARE)) {
        AT91C_BASE_US0->US_CR = AT91C_US_RSTSTA;
    }
    
    while (AT91C_BASE_US0->US_CSR & AT91C_US_RXRDY) {
        uint8_t byte = AT91C_BASE_US0->US_RHR & 0xFF;

        switch (rx_state) {
            case 0:  // Ждём маркер 0xCC
                if (byte == CMD_MARKER) {
                    rx_state = 1;
                }
                break;
                
            case 1:  // Читаем команду
                rx_cmd = byte;
                rx_state = 2;
                break;
                
            case 2:  // Читаем аргумент
                rx_arg = byte;
                // < ИСПРАВЛЕНО: Передаём два отдельных байта, а не указатель
                process_command(rx_cmd, rx_arg);
                rx_state = 0;
                break;
        }
    }
}

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
    
    if (check_timeout(&last_update, 2000)) {
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
    
    // ========== КАЖДЫЕ 500 мкс: Измерить и сохранить ==========
    if (batch_index < BATCH_SIZE) {
        // Чтение АЦП
        for (int n = 0; n < ADC_CHANNELS; n++) {
            batch_buffer[batch_index].adc[n] = adc_read(n);
        }
        batch_buffer[batch_index].led = led_state;
        
        batch_index++;
        
        // Если батч заполнен - отправить
        if (batch_index >= BATCH_SIZE) {
            send_batch();
        }
    }

    // LED мигание
    short_counter++;
    if (short_counter >= 1000) {
        short_counter = 0;
        if (led_state) {
            AT91C_BASE_PIOA->PIO_SODR = LED_PIN; // Выключить (лог. 1)
            led_state = 0;
        } else {
            AT91C_BASE_PIOA->PIO_CODR = LED_PIN;  // Включить (лог. 0)
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
    AT91C_BASE_PITC->PITC_PIMR = AT91C_PITC_PITEN | AT91C_PITC_PITIEN | 867;

    usart0_init(256000); // для приема команд
    usart1_init(256000); // для отправки данных АЦП
    mcp4726_init_all();
    
    __enable_interrupt();

    while (1) {
        check_usart_rx();  // читаем команды с usart0
        
        if (g_test_sequential_enabled) {
            test_dac_sequential_channels();
        }
    }
}