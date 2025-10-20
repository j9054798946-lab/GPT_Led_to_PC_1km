/*
 * Version: 2012
 * AT91SAM7S256 - LED blink + UART
 * ��������� ���������� �������� UART
 * 20.10.2025 �������� �������� �� 230400
*/
#include "AT91SAM7S256.h"
#include <intrinsics.h>

#define LED_PIN (1 << 7)

#define PIN_CS     (1 << 12)
#define PIN_DATA   (1 << 15)
#define PIN_SCLK   (1 << 25)

//---------------- ADC init -----------------
void adc_init(void)
{
    // ��������� ���������� ������ ����� PIO
    AT91C_BASE_PIOA->PIO_PER = PIN_CS | PIN_DATA | PIN_SCLK;

    // CS � SCLK ��� ������, DATA ��� ����
    AT91C_BASE_PIOA->PIO_OER = PIN_CS | PIN_SCLK;
    AT91C_BASE_PIOA->PIO_ODR = PIN_DATA;

    // ������� �� ���������
    AT91C_BASE_PIOA->PIO_SODR = PIN_CS;    // CS=1
    AT91C_BASE_PIOA->PIO_CODR = PIN_SCLK;  // SCLK=0
}
//------------------- ADC read -----------------
unsigned short adc_read(unsigned int adc_num)
{
    unsigned short value = 0;
    unsigned int data_pin;
    int i;

    // ����� ���� DATA
    switch (adc_num) {
        case 0: data_pin = (1 << 15); break; // PA15
        case 1: data_pin = (1 << 24); break; // PA24
        case 2: data_pin = (1 << 10); break; // PA10
        case 3: data_pin = (1 << 29); break; // PA29
        default: data_pin = (1 << 15);       // �� ��������� ������
    }

    // 1. ��������� ���� ���������
    AT91C_BASE_PIOA->PIO_CODR = PIN_CS;   // CS=0

    // 2. ���������� ��������� 4 �����
    for (i = 0; i < 4; i++) {
        AT91C_BASE_PIOA->PIO_CODR = PIN_SCLK;
        AT91C_BASE_PIOA->PIO_SODR = PIN_SCLK;
    }

    // 3. ������� 16����
    for (i = 0; i < 16; i++) {
        AT91C_BASE_PIOA->PIO_CODR = PIN_SCLK;
        AT91C_BASE_PIOA->PIO_SODR = PIN_SCLK;

        value <<= 1;
        if (AT91C_BASE_PIOA->PIO_PDSR & data_pin)
            value |= 1;
    }

    // 4. ��������� ����
    AT91C_BASE_PIOA->PIO_SODR = PIN_CS;   // CS�=�1

    return value;
}

//------------------------------------------------
volatile unsigned int led_state = 0;
volatile unsigned int pit_counter = 0;

// ���������� �������� MCK �� ��������� PIT
// PIT Clock = 86727 / 0.050 = 1734540 Hz
// MCK = PIT Clock * 16 = 27752640 Hz
#define MCK 27752640UL

// ---------------- USART0 ----------------
void usart0_init(unsigned int baud)
{
    unsigned int cd;

    // �������� ���� USART0
    AT91C_BASE_PMC->PMC_PCER = (1 << AT91C_ID_US0);

    // ��������� ���� PA5=TXD0 � PA6=RXD0 ��� Peripheral A
    AT91C_BASE_PIOA->PIO_PDR = (1<<5) | (1<<6);
    AT91C_BASE_PIOA->PIO_ASR = (1<<5) | (1<<6);

    // Reset and disable TX/RX
    AT91C_BASE_US0->US_CR = AT91C_US_RSTRX | AT91C_US_RSTTX
                          | AT91C_US_RXDIS | AT91C_US_TXDIS;

    // Mode: ��������, CLKS=MCK, 8N1
    AT91C_BASE_US0->US_MR = AT91C_US_USMODE_NORMAL
                          | AT91C_US_CLKS_CLOCK
                          | AT91C_US_CHRL_8_BITS
                          | AT91C_US_PAR_NONE
                          | AT91C_US_NBSTOP_1_BIT;

    // ���������� ������ BRGR � �������� MCK
    cd = (MCK + (baud * 8)) / (16 * baud);
    AT91C_BASE_US0->US_BRGR = cd;

    // Enable TX/RX
    AT91C_BASE_US0->US_CR = AT91C_US_TXEN | AT91C_US_RXEN;
}

// �������� ������ �������
void usart0_putc(char c)
{
    while (!(AT91C_BASE_US0->US_CSR & AT91C_US_TXRDY));
    AT91C_BASE_US0->US_THR = c;
}

// ---------------- PIT ----------------
void PIT_Handler(void)
{
    volatile unsigned int dummy = AT91C_BASE_PITC->PITC_PIVR;
    (void)dummy;

    pit_counter++;

    if (pit_counter >= 10) {
        pit_counter = 0;

        if (led_state) {
            AT91C_BASE_PIOA->PIO_SODR = LED_PIN;
            led_state = 0;
            usart0_putc('0');
        } else {
            AT91C_BASE_PIOA->PIO_CODR = LED_PIN;
            led_state = 1;
            usart0_putc('1');
        }

        // === ����� �����: ������ 4���� ===
        unsigned short adc[4];
        for (int n=0; n<4; n++)
            adc[n] = adc_read(n);

        // ��������� �����: 8�����=�4�?�2
        for (int n=0; n<4; n++) {
            usart0_putc((adc[n] >> 8) & 0xFF);
            usart0_putc(adc[n] & 0xFF);
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
    // ��������� watchdog
    AT91C_BASE_WDTC->WDTC_WDMR = AT91C_WDTC_WDDIS;

    // ������������� LED
    AT91C_BASE_PMC->PMC_PCER = (1 << AT91C_ID_PIOA);
    AT91C_BASE_PIOA->PIO_PER = LED_PIN;
    AT91C_BASE_PIOA->PIO_OER = LED_PIN;
    AT91C_BASE_PIOA->PIO_SODR = LED_PIN; // OFF �� ���������
 
    adc_init();
    
    // AIC: ��������� SYS-���������� (PIT)
    AT91C_BASE_AIC->AIC_IDCR = (1 << AT91C_ID_SYS);
    AT91C_BASE_AIC->AIC_ICCR = (1 << AT91C_ID_SYS);
    AT91C_BASE_AIC->AIC_IECR = (1 << AT91C_ID_SYS);

    // ������ �������� PIT ��� 50ms
    AT91C_BASE_PITC->PITC_PIMR = AT91C_PITC_PITEN
                               | AT91C_PITC_PITIEN
                               | 86727;

    // ������������� USART0 � ���������� MCK
    usart0_init(256000);
    
    // �������� �������� ��� ������
    usart0_putc('T');
    usart0_putc('E');
    usart0_putc('S');
    usart0_putc('T');
    usart0_putc('\r');
    usart0_putc('\n');
    
    __enable_interrupt();

    while (1) {
        // �� � ����������� (LED+UART)
    }
}