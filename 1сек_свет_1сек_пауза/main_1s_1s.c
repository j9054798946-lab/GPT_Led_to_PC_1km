/*
 * Version: 1011
 * Debug: проверка формирования интервала ~1s/1s через программный счетчик
 *        (PIT даёт тик ~100мс, а мы считаем до 10 тиков).
 */

#include "AT91SAM7S256.h"
#include <intrinsics.h>

#define LED_PIN (1 << 7)

volatile unsigned int led_state     = 0;
volatile unsigned int shadow_pisr   = 0;   // зеркало статуса
volatile unsigned int shadow_pivr   = 0;   // зеркало значения
volatile unsigned int tick_counter  = 0;   // счётчик тиков для формирования 1s

/* ----------- PIT_Handler ----------- */
void PIT_Handler(void)
{
    // Сбросим флаг (чтением PIVR) и сохраним в глобал
    shadow_pivr = AT91C_BASE_PITC->PITC_PIVR;
    
    // ← программный счётчик по ~100мс тикам
    tick_counter++;
    if (tick_counter >= 10) {        // ~1 секунда прошла
        tick_counter = 0;
        if (led_state) {
            AT91C_BASE_PIOA->PIO_SODR = LED_PIN; // OFF
            led_state = 0;
        } else {
            AT91C_BASE_PIOA->PIO_CODR = LED_PIN; // ON
            led_state = 1;
        }
    }
}

/* ----------- IRQ_Handler ----------- */
__irq void IRQ_Handler(void)
{
    shadow_pisr = AT91C_BASE_PITC->PITC_PISR;
    if (shadow_pisr & AT91C_PITC_PITS) {
        PIT_Handler();
    }
    AT91C_BASE_AIC->AIC_EOICR = 0;
}

/* ----------- MAIN ----------- */
int main(void)
{
    AT91C_BASE_WDTC->WDTC_WDMR = AT91C_WDTC_WDDIS; //disable watchdog

    // LED init
    AT91C_BASE_PMC->PMC_PCER = (1 << AT91C_ID_PIOA);
    AT91C_BASE_PIOA->PIO_PER = LED_PIN;
    AT91C_BASE_PIOA->PIO_OER = LED_PIN;
    AT91C_BASE_PIOA->PIO_SODR = LED_PIN; // OFF по умолчанию

    // AIC настройка прерываний для SYS
    AT91C_BASE_AIC->AIC_IDCR = (1 << AT91C_ID_SYS);
    AT91C_BASE_AIC->AIC_ICCR = (1 << AT91C_ID_SYS);
    AT91C_BASE_AIC->AIC_IECR = (1 << AT91C_ID_SYS);

    // PIT: выберем Reload ~300000 (≈ 0.1 сек при 48 МГц MCK)
    AT91C_BASE_PITC->PITC_PIMR =
        AT91C_PITC_PITEN | AT91C_PITC_PITIEN | 300000;

    __enable_interrupt();

    while (1) {
        // пустой цикл, обработка вся в прерываниях
    }
}