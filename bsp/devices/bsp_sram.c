//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "bsp.h"
#include "bsp_sram.h"

//============================================================================
static void BSP_sramGpioInit_(void);
static void BSP_sramFsmcInit_(void);

//============================================================================
void BSP_sramInit(void) {
    BSP_sramGpioInit_();
    BSP_sramFsmcInit_();
}

//............................................................................
static void BSP_sramGpioInit_(void) {
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_FSMC_CLK_ENABLE();

    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;   // A16 A17 A18
    HAL_GPIO_Init(GPIOD, &gpio);

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;                   // NBL0 NBL1
    HAL_GPIO_Init(GPIOE, &gpio);

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3
             | GPIO_PIN_4 | GPIO_PIN_5
             | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOF, &gpio);                          // A0..A9

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3
             | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOG, &gpio);                          // A10..A15, NE3
}

//............................................................................
static void BSP_sramFsmcInit_(void) {
    static SRAM_HandleTypeDef sram;
    FSMC_NORSRAM_TimingTypeDef read_timing = {0};
    FSMC_NORSRAM_TimingTypeDef write_timing = {0};

    sram.Instance = FSMC_NORSRAM_DEVICE;
    sram.Extended = FSMC_NORSRAM_EXTENDED_DEVICE;
    sram.Init.NSBank = FSMC_NORSRAM_BANK3;
    sram.Init.DataAddressMux = FSMC_DATA_ADDRESS_MUX_DISABLE;
    sram.Init.MemoryType = FSMC_MEMORY_TYPE_SRAM;
    sram.Init.MemoryDataWidth = FSMC_NORSRAM_MEM_BUS_WIDTH_16;
    sram.Init.BurstAccessMode = FSMC_BURST_ACCESS_MODE_DISABLE;
    sram.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    sram.Init.WrapMode = FSMC_WRAP_MODE_DISABLE;
    sram.Init.WaitSignalActive = FSMC_WAIT_TIMING_BEFORE_WS;
    sram.Init.WriteOperation = FSMC_WRITE_OPERATION_ENABLE;
    sram.Init.WaitSignal = FSMC_WAIT_SIGNAL_DISABLE;
    sram.Init.ExtendedMode = FSMC_EXTENDED_MODE_ENABLE;
    sram.Init.AsynchronousWait = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    sram.Init.WriteBurst = FSMC_WRITE_BURST_DISABLE;

    // This is a functional probe, not a bandwidth test, so the timing is
    // deliberately generous: roughly 230 ns per access, far above the access
    // time of the part that is fitted.
    read_timing.AddressSetupTime = 0x01;
    read_timing.DataSetupTime = 0x0F;
    read_timing.BusTurnAroundDuration = 0;
    read_timing.AccessMode = FSMC_ACCESS_MODE_A;

    write_timing.AddressSetupTime = 0x01;
    write_timing.DataSetupTime = 0x0F;
    write_timing.BusTurnAroundDuration = 0;
    write_timing.AccessMode = FSMC_ACCESS_MODE_A;

    HAL_SRAM_Init(&sram, &read_timing, &write_timing);
}
