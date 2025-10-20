        PUBLIC  enable_irq
        ARM

enable_irq:
        MRS     r0, CPSR        ; ����� ������
        BIC     r0, r0, #0x80   ; �������� ��� I (IRQ disable)
        MSR     CPSR_c, r0      ; �������� �������
        BX      lr
        END