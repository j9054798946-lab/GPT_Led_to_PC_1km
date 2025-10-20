        PUBLIC  enable_irq
        ARM

enable_irq:
        MRS     r0, CPSR        ; взять статус
        BIC     r0, r0, #0x80   ; сбросить бит I (IRQ disable)
        MSR     CPSR_c, r0      ; записать обратно
        BX      lr
        END