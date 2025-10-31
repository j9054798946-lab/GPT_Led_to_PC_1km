#include "my_dac.h"

// небольшие микро-задержки
static inline void delay_short(int n)
{
    for (volatile int i=0; i<n; ++i);
}

// инициализаци€ пинов
void dac_init(void)
{
    // разрешаем управление GPIO
    AT91C_BASE_PIOA->PIO_PER = PIN_SYNC | PIN_SCLK |
                               PIN_SDIN1 | PIN_SDIN2 | PIN_SDIN3 | PIN_SDIN4;

    // настраиваем как выходы
    AT91C_BASE_PIOA->PIO_OER = PIN_SYNC | PIN_SCLK |
                               PIN_SDIN1 | PIN_SDIN2 | PIN_SDIN3 | PIN_SDIN4;

    // выставл€ем высокий уровень по умолчанию
    AT91C_BASE_PIOA->PIO_SODR = PIN_SYNC | PIN_SCLK |
                                PIN_SDIN1 | PIN_SDIN2 | PIN_SDIN3 | PIN_SDIN4;
}

// -------------------- MCP4726 ---------------------------
// проста€ быстра€ передача 12-бит кода во все 4 ÷јѕ
void dac_write(unsigned short values[4])
{
    // MCP4726 принимает 16 бит: 4 служебных (0x3) + 12 данных
    unsigned short frame[4];
    for (int i=0; i<4; i++)
        frame[i] = 0x3000 | (values[i] & 0x0FFF);

    // START: SYNC = 0
    AT91C_BASE_PIOA->PIO_CODR = PIN_SYNC;
    delay_short(30);

    // передача 16 бит
    for (int bit=15; bit>=0; bit--)
    {
        // такт SCLK = 0
        AT91C_BASE_PIOA->PIO_CODR = PIN_SCLK;

        // каждый SDIN в соответствии со своим битом кадра
        ((frame[0] >> bit) & 1) ? (AT91C_BASE_PIOA->PIO_SODR = PIN_SDIN1)
                                : (AT91C_BASE_PIOA->PIO_CODR = PIN_SDIN1);
        ((frame[1] >> bit) & 1) ? (AT91C_BASE_PIOA->PIO_SODR = PIN_SDIN2)
                                : (AT91C_BASE_PIOA->PIO_CODR = PIN_SDIN2);
        ((frame[2] >> bit) & 1) ? (AT91C_BASE_PIOA->PIO_SODR = PIN_SDIN3)
                                : (AT91C_BASE_PIOA->PIO_CODR = PIN_SDIN3);
        ((frame[3] >> bit) & 1) ? (AT91C_BASE_PIOA->PIO_SODR = PIN_SDIN4)
                                : (AT91C_BASE_PIOA->PIO_CODR = PIN_SDIN4);

        delay_short(10);
        // фронт SCLK вверх
        AT91C_BASE_PIOA->PIO_SODR = PIN_SCLK;
        delay_short(10);
    }

    // STOP: SYNC = 1
    AT91C_BASE_PIOA->PIO_SODR = PIN_SYNC;
    delay_short(40);
}