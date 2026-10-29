//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "stm32f1xx_hal.h"
#include "bl_boot.h"

static UART_HandleTypeDef Bl_uart_;
static void Bl_clock_(void);
static void Bl_uartInit_(void);

int main(void) {
    HAL_Init();
    Bl_clock_();
    Bl_uartInit_();
    Bl_Boot_run(&Bl_uart_);
}

//============================================================================
void Error_Handler(void) {
    __disable_irq();

    for (;;) {
    }
}
//............................................................................
// A lost DMA completion cannot return a buffer that hardware may still own.
void SM_onAssert(char const *module, int label) {
    (void)module;
    (void)label;
    NVIC_SystemReset();
    for (;;) {
    }
}
//............................................................................
// The TIM1 time base (bsp/platform/stm32f1xx_hal_timebase_tim.c) starts the
// timer and enables the interrupt, but the callback that actually feeds
// HAL_IncTick() has to come from the image: the HAL's own is an empty weak
// one. This image supplies its own callback, or every HAL_Delay() here
// waits forever on a
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
