// Cortex-M3 fault capture. No stack access, C calls, HAL or UART dependency.
#include "bsp_fault_record.h"

.syntax unified
.cpu cortex-m3
.thumb
.section .text.BSP_faultCapture_, "ax", %progbits
.global HardFault_Handler
.global MemManage_Handler
.global BusFault_Handler
.global UsageFault_Handler
.type HardFault_Handler, %function
.thumb_func
HardFault_Handler:
    ldr r0, =_sfault
    movs r1, #0
    str r1, [r0, #BSP_FAULT_SEAL_OFFSET]
    dmb
    add r1, r0, #48
    stmia r1, {r4-r11}
    // R0-R3/R12 are already in the hardware frame, if stacking succeeded.
    mrs r1, msp
    str r1, [r0, #80]
    mrs r2, psp
    str r2, [r0, #84]
    str lr, [r0, #88]
    mrs r3, control
    str r3, [r0, #92]
    mrs r3, primask
    str r3, [r0, #96]
    cpsid i
    mrs r3, basepri
    str r3, [r0, #100]
    mrs r3, faultmask
    str r3, [r0, #104]
    mrs r3, ipsr
    str r3, [r0, #8]
    ldr r4, =0xE000ED00
    ldr r3, [r4, #0x28]       // CFSR, before any original-frame access
    str r3, [r0, #12]
    ldr r5, [r4, #0x2C]
    str r5, [r0, #16]         // HFSR
    ldr r5, [r4, #0x38]
    str r5, [r0, #20]         // BFAR (interpret only with BFARVALID)
    ldr r5, [r4, #0x34]
    str r5, [r0, #108]        // MMFAR (interpret only with MMARVALID)
    ldr r5, [r4, #0x24]
    str r5, [r0, #112]        // SHCSR

    // Clear optional frame fields even when the previous stash was valid.
    movs r5, #0
    str r5, [r0, #0]
    str r5, [r0, #4]
    str r5, [r0, #116]        // flags: frame not captured
    add r6, r0, #24
    movs r7, #6
1:  str r5, [r6], #4
    subs r7, #1
    bne 1b

    // Reject any stacking/unstacking error and malformed EXC_RETURN.
    ldr r5, =0x00001818
    tst r3, r5
    bne 3f
    ldr r5, =0xFFFFFFF1
    cmp lr, r5
    beq 2f
    adds r5, #8
    cmp lr, r5
    beq 2f
    adds r5, #4
    cmp lr, r5
    bne 3f
2:  tst lr, #4
    it ne
    movne r1, r2
    tst r1, #3
    bne 3f
    // Only internal SRAM is eligible; external SRAM may not be ready.
    ldr r5, =0x20000000
    cmp r1, r5
    blo 3f
    sub r5, r0, #32
    cmp r1, r5
    bhi 3f
    ldr r5, [r1, #0]
    str r5, [r0, #24]
    ldr r5, [r1, #4]
    str r5, [r0, #28]
    ldr r5, [r1, #8]
    str r5, [r0, #32]
    ldr r5, [r1, #12]
    str r5, [r0, #36]
    ldr r5, [r1, #16]
    str r5, [r0, #40]
    ldr r5, [r1, #20]
    str r5, [r0, #4]
    ldr r5, [r1, #24]
    str r5, [r0, #0]
    ldr r5, [r1, #28]
    str r5, [r0, #44]
    movs r5, #BSP_FAULT_FRAME_VALID
    str r5, [r0, #116]

3:  ldr r1, =BSP_FAULT_SEED
    ldr r2, =16777619
    mov r3, r0
    movs r4, #BSP_FAULT_PAYLOAD_WORDS
4:  ldr r5, [r3], #4
    eors r1, r5
    mul r1, r1, r2
    subs r4, #1
    bne 4b
    cmp r1, #0
    it eq
    mvneq r1, #0
    dmb
    str r1, [r0, #BSP_FAULT_SEAL_OFFSET]
    dsb

    // Preserve the two-blink fault signature without calling C or using MSP.
    ldr r0, =0x40010C10       // GPIOB BSRR; PB5 is the existing fault LED
    movs r1, #2
5:  movs r2, #32
    str r2, [r0]
    ldr r3, =400000
6:  subs r3, #1
    bne 6b
    lsl r2, r2, #16
    str r2, [r0]
    ldr r3, =400000
7:  subs r3, #1
    bne 7b
    subs r1, #1
    bne 5b

    // SYSRESETREQ, preserving PRIGROUP. No tick or interrupt is required.
    ldr r0, =0xE000ED0C
    ldr r1, [r0]
    and r1, r1, #0x700
    ldr r2, =0x05FA0004
    orrs r1, r2
    dsb
    str r1, [r0]
    dsb
8:  b 8b
.size HardFault_Handler, .-HardFault_Handler
.thumb_set MemManage_Handler, HardFault_Handler
.thumb_set BusFault_Handler, HardFault_Handler
.thumb_set UsageFault_Handler, HardFault_Handler
.section .note.GNU-stack, "", %progbits
