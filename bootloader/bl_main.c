//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
//============================================================================
//=== Bootloader -- the first 16 KB of the flash
//
// It owns two things:
//
//   * The cross-reset flag. The linker script reserves the top 32 bytes of RAM
//     (_snoinit) and the startup code does not clear them, so a word survives
//     a reset (not a power cycle). Writing 0xB00710AD there asks for the next
//     reset to land here instead of in the app: J-Link writes it from the PC
//     (tools/enter-burn.jlink.tpl), and this program clears it when the work
//     is done.
//   * The decision. Flag set, or a handshake inside the boot window, means the
//     board stays here and serves the asset protocol (the receive state machine
//     is docs/diagrams/uart-recv-fsm.mmd; the conversation is
//     docs/diagrams/asset-burn-bootloader.mmd). Otherwise the app gets the CPU.
//
// The hand-over is the part that has to be right. On a real reset the hardware
// loads MSP and PC out of the vector table; nothing does that for a jump we
// make ourselves, so VTOR, MSP and PC are set by hand below.
//
// Not here yet: the protocol itself (the skeleton stops in burn mode and says
// so on the wire), and the NOR flash work that needs it.
//============================================================================
#include "stm32f1xx_hal.h"

#include <stdbool.h>
#include <string.h>

//============================================================================
// What the two linker scripts and the app's main() agree on.
#define BL_APP_BASE    0x08004000U  // app image: flash + 16 KB
#define BL_FLASH_END   0x08080000U  // 512 KB part, one past the last byte
#define BL_RAM_BASE    0x20000000U
#define BL_RAM_END     0x20010000U  // 64 KB, _estack sits 32 bytes below

#define BL_FLAG_MAGIC  0xB00710ADU
#define BL_BOOT_MS     200U         // handshake window for the RESET key

// Written by J-Link, cleared here. Same address in both images (see the .ld).
extern uint32_t _snoinit;

static UART_HandleTypeDef Bl_uart_;

//============================================================================
static void Bl_clock_(void);
static void Bl_uartInit_(void);
static void Bl_say_(char const *text);
static bool Bl_appSane_(void);
static void Bl_handOver_(void) __attribute__((noreturn));

//============================================================================
int main(void) {
    HAL_Init();
    Bl_clock_();
    Bl_uartInit_();

    Bl_say_("\r\n[bootloader] up\r\n");

    if (_snoinit == BL_FLAG_MAGIC) {
        Bl_say_("[bootloader] flag set: burn mode, protocol still TODO\r\n");
        for (;;) {
        }
    }

    // No flag: a normal start. The window gives whoever pressed the board's
    // RESET key a chance to get in. 200 ms against a cold start that already
    // spends 3~4 s on the panel is nothing.
    HAL_Delay(BL_BOOT_MS);

    if (!Bl_appSane_()) {
        Bl_say_("[bootloader] app vector table looks wrong: staying put\r\n");
        for (;;) {
        }
    }

    Bl_say_("[bootloader] handing over to the app\r\n");
    Bl_handOver_();
}

//============================================================================
void Error_Handler(void) {
    __disable_irq();

    for (;;) {
    }
}
//............................................................................
// The TIM1 time base (bsp/platform/stm32f1xx_hal_timebase_tim.c) starts the
// timer and enables the interrupt, but the callback that actually feeds
// HAL_IncTick() has to come from the image: the HAL's own is an empty weak
// one. In the app it lives in application/main.c; a bootloader that links that
// file nowhere needs its own, or every HAL_Delay() here waits forever on a
// uwTick that never moves.
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM1) {
        HAL_IncTick();
    }
}
//............................................................................
// Same clock the app runs at: 72 MHz from HSE + PLL x9, with the two wait
// states 72 MHz needs. Anything else here would change the UART baud rate
// under the PC's feet.
static void Bl_clock_(void) {
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.HSIState = RCC_HSI_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        Error_Handler();
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                    | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        Error_Handler();
    }
}
//............................................................................
// TX and RX: the app only traces (TX), this side has to listen. The CH340's
// control lines stay deassert unless a tool asserts them, and the PC side is
// expected to keep them that way (AGENTS.md, "CH340C 自动下载支路的坑").
static void Bl_uartInit_(void) {
    Bl_uart_.Instance = USART1;
    Bl_uart_.Init.BaudRate = 115200;
    Bl_uart_.Init.WordLength = UART_WORDLENGTH_8B;
    Bl_uart_.Init.StopBits = UART_STOPBITS_1;
    Bl_uart_.Init.Parity = UART_PARITY_NONE;
    Bl_uart_.Init.Mode = UART_MODE_TX_RX;
    Bl_uart_.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    Bl_uart_.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&Bl_uart_) != HAL_OK) {
        Error_Handler();
    }
}
//............................................................................
static void Bl_say_(char const *text) {
    (void)HAL_UART_Transmit(&Bl_uart_, (uint8_t *)(uintptr_t)text,
                            (uint16_t)strlen(text), 100U);
}
//............................................................................
// Cheap sanity check on the image we are about to hand the CPU to. Both words
// matter: a stack pointer outside RAM, or a reset vector with the Thumb bit
// clear, faults on the first instruction fetch, and a debugger is then the
// only way back in.
static bool Bl_appSane_(void) {
    uint32_t const sp = *(uint32_t const *)BL_APP_BASE;
    uint32_t const pc = *(uint32_t const *)(BL_APP_BASE + 4U);

    if ((sp < BL_RAM_BASE) || (sp > BL_RAM_END) || ((sp & 0x3U) != 0U)) {
        return false;
    }
    if ((pc < BL_APP_BASE) || (pc >= BL_FLASH_END) || ((pc & 0x1U) == 0U)) {
        return false;
    }

    return true;
}
//............................................................................
static void Bl_handOver_(void) {
    uint32_t const sp = *(uint32_t const *)BL_APP_BASE;
    uint32_t const pc = *(uint32_t const *)(BL_APP_BASE + 4U);
    uint8_t i;

    __disable_irq();

    // Give the peripherals back the way the app expects to find them at a cold
    // start: this program opened UART1 and the TIM1 time base, and left no IRQ
    // pending. HAL_DeInit() also takes the SysTick clock off `uwTick`, so the
    // app starts its own time base from zero.
    HAL_UART_DeInit(&Bl_uart_);
    HAL_DeInit();

    for (i = 0U; i < 8U; ++i) {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    SCB->VTOR = BL_APP_BASE;
    __set_MSP(sp);
    __enable_irq();
    ((void (*)(void))pc)();

    for (;;) {      // the app never returns
    }
}
