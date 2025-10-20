;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Part one of the system initialization code,
;; contains low-level
;; initialization.
;;

        MODULE  ?cstartup

        ;; Forward declaration of sections.
        SECTION IRQ_STACK:DATA:NOROOT(3)
        SECTION FIQ_STACK:DATA:NOROOT(3)
        SECTION CSTACK:DATA:NOROOT(3)

        SECTION .intvec:CODE:NOROOT(2)

        PUBLIC  __vector
        PUBLIC  __iar_program_start
        EXTERN  Undefined_Handler
        EXTERN  SWI_Handler
        EXTERN  Prefetch_Handler
        EXTERN  Abort_Handler
        EXTERN  IRQ_Handler
        EXTERN  FIQ_Handler

        DATA

__iar_init$$done:               

__vector:                       

        ARM

        LDR     PC,Reset_Addr           ; Reset
        LDR     PC,Undefined_Addr       ; Undefined instructions
        LDR     PC,SWI_Addr             ; Software interrupt (SWI/SVC)
        LDR     PC,Prefetch_Addr        ; Prefetch abort
        LDR     PC,Abort_Addr           ; Data abort
        DCD     0                       ; RESERVED
        LDR     PC,IRQ_Addr             ; IRQ
        LDR     PC,FIQ_Addr             ; FIQ

        DATA

Reset_Addr:     DCD   __iar_program_start
Undefined_Addr: DCD   Undefined_Handler
SWI_Addr:       DCD   SWI_Handler
Prefetch_Addr:  DCD   Prefetch_Handler
Abort_Addr:     DCD   Abort_Handler
IRQ_Addr:       DCD   IRQ_Handler
FIQ_Addr:       DCD   FIQ_Handler


; --------------------------------------------------
; ?cstartup -- low-level system initialization code.
; After reset execution starts here, mode is ARM/SVC, ints disabled.
; --------------------------------------------------

        SECTION .text:CODE:NOROOT(2)

        EXTERN  __cmain
        REQUIRE __vector
        EXTWEAK __iar_init_core
        EXTWEAK __iar_init_vfp

        PUBLIC  low_level_init          ; *** NEW ***

        ARM

__iar_program_start:
?cstartup:

; --------------------- стеки ---------------------
#define MODE_MSK 0x1F
#define USR_MODE 0x10
#define FIQ_MODE 0x11
#define IRQ_MODE 0x12
#define SVC_MODE 0x13
#define ABT_MODE 0x17
#define UND_MODE 0x1B
#define SYS_MODE 0x1F

        MRS     r0, cpsr

        ; IRQ stack
        BIC     r0, r0, #MODE_MSK
        ORR     r0, r0, #IRQ_MODE
        MSR     cpsr_c, r0
        LDR     r1, =SFE(IRQ_STACK)
        BIC     sp,r1,#0x7

        ; FIQ stack
        BIC     r0, r0, #MODE_MSK
        ORR     r0, r0, #FIQ_MODE
        MSR     cpsr_c, r0
        LDR     r1, =SFE(FIQ_STACK)
        BIC     sp,r1,#0x7

        ; SYS stack
        BIC     r0 ,r0, #MODE_MSK
        ORR     r0 ,r0, #SYS_MODE
        MSR     cpsr_c, r0
        LDR     r1, =SFE(CSTACK)
        BIC     sp,r1,#0x7

        ; init core
        FUNCALL __iar_program_start, __iar_init_core
        BL      __iar_init_core

        ; init VFP
        FUNCALL __iar_program_start, __iar_init_vfp
        BL      __iar_init_vfp

        ; *** Вызов настройки тактов ***
        BL      low_level_init          ; *** NEW ***

        ; --- переход в C ---
        FUNCALL __iar_program_start, __cmain
        B       __cmain

; --------------------------------------------------
; Функция low_level_init для 18.432 MHz кварца
; --------------------------------------------------
; Настроит PLL так, чтобы получить MCK ≈ 48 MHz.
; Алгоритм:
;  - включаем главный осциллятор
;  - настраиваем PLL (DIV=14, MUL=72 → ≈94 MHz)
;  - переключаем MCK на PLL/2 ≈ 47 MHz
; --------------------------------------------------

low_level_init:
        ; Включаем Main OSC
        LDR     r0, =0xFFFFFC20         ; CKGR_MOR
        LDR     r1, =0x00000601         ; MOSCEN=1, OSCOUNT=0xFF
        STR     r1, [r0]
mosc_wait:
        LDR     r1, =0xFFFFFC68         ; PMC_SR
        LDR     r2, [r1]
        TST     r2, #0x01               ; MOSCS
        BEQ     mosc_wait

        ; Настроим PLL: DIV=14, MUL=72
        LDR     r0, =0xFFFFFC2C         ; CKGR_PLLR
        LDR     r1, =0x20293E0E         ; value = MUL<<16 | PLLCOUNT<<8 | DIV
        ;LDR     r1, =0x00193F05     ; DIV=5, MUL=25, Fpll ≈ 95.8464 МГц
        ;LDR     r1, =0x00253F05
        STR     r1, [r0]
pll_wait:
        LDR     r1, =0xFFFFFC68
        LDR     r2, [r1]
        TST     r2, #0x04               ; LOCK
        BEQ     pll_wait

        ; Переключаем MCK сперва на MAINCK
        LDR     r0, =0xFFFFFC30         ; PMC_MCKR
        MOV     r1, #0x01               ; CSS=MAIN
        STR     r1, [r0]
mck_ready1:
        LDR     r2, =0xFFFFFC68
        LDR     r3, [r2]
        TST     r3, #0x08               ; MCKRDY
        BEQ     mck_ready1

        ; А теперь CSS=PLL, PRES=DIV2
        MOV     r1, #0x07               ; CSS=PLL, PRES=2
        STR     r1, [r0]
mck_ready2:
        LDR     r2, =0xFFFFFC68
        LDR     r3, [r2]
        TST     r3, #0x08
        BEQ     mck_ready2

        MOV     pc, lr

        END