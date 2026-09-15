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
#define SRAM_BASE_ADDR  0x68000000U
#define SRAM_16         ((volatile uint16_t *)SRAM_BASE_ADDR)

// Address lines A0..A18 of the bank; the SRAM is 512K x 16.
#define SRAM_LINES      19U

// Two marks that differ in every bit, so a lost or aliased cell cannot hide.
#define SRAM_MARK_BASE  0xAAAAU
#define SRAM_MARK_EDGE  0x5555U

// Trace records: ==SRAM==[LINE][OK]==
//                ==SRAM==[LINE][BAD][bit ..][base ..]==  address line aliases
//                ==SRAM==[LINE][BAD][bit ..][edge ..]==  cell did not hold
#define SRAM_TRACE_OK   17U
#define SRAM_TRACE_BAD  18U
#define SRAM_TRACE_CELL 19U

//============================================================================
static void BSP_sramGpioInit_(void);
static void BSP_sramFsmcInit_(void);
static void BSP_sramTraceBad_(uint8_t recId, uint8_t bit, uint16_t read);

//============================================================================
void BSP_sramInit(void) {
    BSP_sramGpioInit_();
    BSP_sramFsmcInit_();
}

//............................................................................
void BSP_sramProbe(void) {
    uint8_t bit;

    for (bit = 0U; bit < SRAM_LINES; ++bit) {
        uint32_t const offset = (uint32_t)1U << bit; // Toggles that address line.
        uint16_t read;

        SRAM_16[0] = SRAM_MARK_BASE;
        SRAM_16[offset] = SRAM_MARK_EDGE;

        // The edge cell first: if it did not hold, the write path or the cell
        // itself is at fault. If it did hold but the base cell lost its mark,
        // the two addresses aliased, which is what a stuck or shorted address
        // line does.
        read = SRAM_16[offset];
        if (read != SRAM_MARK_EDGE) {
            BSP_sramTraceBad_(SRAM_TRACE_CELL, bit, read);
            return;
        }

        read = SRAM_16[0];
        if (read != SRAM_MARK_BASE) {
            BSP_sramTraceBad_(SRAM_TRACE_BAD, bit, read);
            return;
        }
    }

    SM_TRACE_BEGIN(SRAM_TRACE_OK, 0)
    SM_TRACE_END()
}

//............................................................................
static void BSP_sramTraceBad_(uint8_t recId, uint8_t bit, uint16_t read) {
    SM_TRACE_BEGIN(recId, 3)
        SM_TRACE_PAYLOAD_BE(bit)
        SM_TRACE_PAYLOAD_BE(read >> 8)
        SM_TRACE_PAYLOAD_BE(read >> 0)
    SM_TRACE_END()
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
