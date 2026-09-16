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
#include "bsp_lcd.h"

//============================================================================
#define LCD_BANK_ADDR   0x6C000000U
#define LCD_CMD_ADDR    (LCD_BANK_ADDR | 0x7FEU)  // RS = FSMC_A10 = 0
#define LCD_DAT_ADDR    (LCD_BANK_ADDR | 0x800U)  // RS = FSMC_A10 = 1
#define LCD_CMD         (*(volatile uint16_t *)LCD_CMD_ADDR)
#define LCD_DAT         (*(volatile uint16_t *)LCD_DAT_ADDR)

#define LCD_RDID_CMD    0x04U
#define LCD_SLPOUT_CMD  0x11U  // Frame memory is unreachable while asleep.
#define LCD_COLMOD_16BIT 0x05U // MCU interface colour format: 16 bit/pixel.

#define LCD_ID_ST7789   0x8552U

// Panel geometry; the display layer will address the panel through these.
#define LCD_WIDTH       240U
#define LCD_HEIGHT      320U

#define LCD_BL_PORT     GPIOB
#define LCD_BL_PIN      GPIO_PIN_0

// Panel waits. RST is tied to the board reset net (bsp_lcd.h), so the panel
// has been in reset recovery since the MCU left reset: the datasheet wants no
// command for the first 5 ms and no SLPOUT before 120 ms (Note 7, p.49). The
// frame memory stays unreachable for a further 120 ms after SLPOUT; the
// vendor sequence waits 120, we keep 130.
#define LCD_RESET_SETTLE_MS  120U
#define LCD_SLPOUT_SETTLE_MS 130U

// Trace records: ==LCD==[RDID][<hi> <lo>]==[ST7789/??????]==
//                ==LCD==[DISPON]==
#define LCD_TRACE_RDID_OK   12U
#define LCD_TRACE_RDID_BAD  13U
#define LCD_TRACE_DISP_ON   20U

//============================================================================
// Panel register sequence: the vendor table for this module (ALIENTEK
// atk_md0240_reg_init). SLPOUT is not part of it; BSP_lcdInit() sends that
// first, then waits the panel's sleep-out time out.
typedef struct {
    uint16_t cmd;
    uint8_t argCount;
    uint8_t args[14];
} BSP_lcdInitStep_;

static BSP_lcdInitStep_ const lcdInitSeq_[] = {
    {0x36U,  1U, {0x00U}},                                // MADCTL
    {0x3AU,  1U, {LCD_COLMOD_16BIT}},                     // pixel format
    {0xB2U,  5U, {0x0CU, 0x0CU, 0x00U, 0x33U, 0x33U}},    // porch
    {0xB7U,  1U, {0x35U}},                                // gate control
    {0xBBU,  1U, {0x32U}},                                // VCOM
    {0xC0U,  1U, {0x0CU}},                                // power control
    {0xC2U,  1U, {0x01U}},                                // VDVRH enable
    {0xC3U,  1U, {0x10U}},                                // VRH
    {0xC4U,  1U, {0x20U}},                                // VDV
    {0xC6U,  1U, {0x0FU}},                                // frame rate
    {0xD0U,  2U, {0xA4U, 0xA1U}},                         // power control 2
    {0xE0U, 14U, {0xD0U, 0x00U, 0x02U, 0x07U, 0x0AU, 0x28U, 0x32U,
                  0x44U, 0x42U, 0x06U, 0x0EU, 0x12U, 0x14U, 0x17U}},
    {0xE1U, 14U, {0xD0U, 0x00U, 0x02U, 0x07U, 0x0AU, 0x28U, 0x31U,
                  0x54U, 0x47U, 0x0EU, 0x1CU, 0x17U, 0x1BU, 0x1EU}},
    {0x21U,  0U, {0x00U}},                                // inversion on
    {0x29U,  0U, {0x00U}}                                 // display on
};

//============================================================================
static void BSP_lcdGpioInit_(void);
static void BSP_lcdFsmcInit_(void);
static void BSP_lcdWriteCmd_(uint16_t cmd);
static void BSP_lcdWriteDat_(uint16_t dat);
static uint16_t BSP_lcdReadDat_(void);
static uint16_t BSP_lcdReadId_(void);
static void BSP_lcdPanelInit_(void);
static void BSP_lcdTraceId_(void);

//============================================================================
void BSP_lcdInit(void) {
    BSP_lcdGpioInit_();
    BSP_lcdFsmcInit_();

    // Backlight on, so the panel power path is observable on the bench.
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_SET);

    // The scheduler is not up yet, so HAL_Delay() - which runs off the TIM1
    // HAL tick, alive since HAL_Init() - is the only wait available here.
    uint32_t const sinceReset = HAL_GetTick();

    if (sinceReset < LCD_RESET_SETTLE_MS) {
        HAL_Delay(LCD_RESET_SETTLE_MS - sinceReset);
    }

    BSP_lcdWriteCmd_(LCD_SLPOUT_CMD);
    HAL_Delay(LCD_SLPOUT_SETTLE_MS);

    BSP_lcdPanelInit_();
    SM_TRACE_BEGIN(LCD_TRACE_DISP_ON, 0)
    SM_TRACE_END()

    // One identification read: the "panel is talking" line on the bench.
    BSP_lcdTraceId_();
}

//............................................................................
static void BSP_lcdPanelInit_(void) {
    uint8_t step;

    for (step = 0U;
         step < (uint8_t)(sizeof(lcdInitSeq_) / sizeof(lcdInitSeq_[0]));
         ++step) {
        uint8_t arg;

        BSP_lcdWriteCmd_(lcdInitSeq_[step].cmd);
        for (arg = 0U; arg < lcdInitSeq_[step].argCount; ++arg) {
            BSP_lcdWriteDat_(lcdInitSeq_[step].args[arg]);
        }
    }
}

//............................................................................
static void BSP_lcdTraceId_(void) {
    uint16_t const id = BSP_lcdReadId_();

    if (id == LCD_ID_ST7789) {
        SM_TRACE_BEGIN(LCD_TRACE_RDID_OK, 2)
            SM_TRACE_PAYLOAD_BE(id >> 8)
            SM_TRACE_PAYLOAD_BE(id)
        SM_TRACE_END()
    } else {
        SM_TRACE_BEGIN(LCD_TRACE_RDID_BAD, 2)
            SM_TRACE_PAYLOAD_BE(id >> 8)
            SM_TRACE_PAYLOAD_BE(id)
        SM_TRACE_END()
    }
}

//............................................................................
static void BSP_lcdGpioInit_(void) {
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_FSMC_CLK_ENABLE();

    // Backlight, active high, idle off.
    gpio.Pin   = LCD_BL_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LCD_BL_PORT, &gpio);
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_RESET);

    // FSMC signals: alternate function push-pull. The panels data bus is read
    // back, so keep the internal pull-ups on to hold the lines defined.
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5
             | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10
             | GPIO_PIN_14 | GPIO_PIN_15;  // D2 D3 NOE NWE D13 D14 D15 D0 D1
    HAL_GPIO_Init(GPIOD, &gpio);

    gpio.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10
             | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13
             | GPIO_PIN_14 | GPIO_PIN_15;  // D4..D12
    HAL_GPIO_Init(GPIOE, &gpio);

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_12;   // A10 (RS), NE4 (CS)
    HAL_GPIO_Init(GPIOG, &gpio);
}

//............................................................................
static void BSP_lcdFsmcInit_(void) {
    static SRAM_HandleTypeDef lcd_sram;
    FSMC_NORSRAM_TimingTypeDef read_timing = {0};
    FSMC_NORSRAM_TimingTypeDef write_timing = {0};

    lcd_sram.Instance = FSMC_NORSRAM_DEVICE;
    lcd_sram.Extended = FSMC_NORSRAM_EXTENDED_DEVICE;
    lcd_sram.Init.NSBank = FSMC_NORSRAM_BANK4;
    lcd_sram.Init.DataAddressMux = FSMC_DATA_ADDRESS_MUX_DISABLE;
    lcd_sram.Init.MemoryType = FSMC_MEMORY_TYPE_SRAM;
    lcd_sram.Init.MemoryDataWidth = FSMC_NORSRAM_MEM_BUS_WIDTH_16;
    lcd_sram.Init.BurstAccessMode = FSMC_BURST_ACCESS_MODE_DISABLE;
    lcd_sram.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    lcd_sram.Init.WrapMode = FSMC_WRAP_MODE_DISABLE;
    lcd_sram.Init.WaitSignalActive = FSMC_WAIT_TIMING_BEFORE_WS;
    lcd_sram.Init.WriteOperation = FSMC_WRITE_OPERATION_ENABLE;
    lcd_sram.Init.WaitSignal = FSMC_WAIT_SIGNAL_DISABLE;
    lcd_sram.Init.ExtendedMode = FSMC_EXTENDED_MODE_ENABLE;
    lcd_sram.Init.AsynchronousWait = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    lcd_sram.Init.WriteBurst = FSMC_WRITE_BURST_DISABLE;

    // Mode A. The read side is not throughput bound - it only runs the boot
    // RDID - so these values buy margin instead of the smallest legal pair.
    // Spec targets (ST7789VW Table 4, Read ID): TRCS >= 45 ns, TRDH >= 90,
    // TRDL >= 45, TRC >= 160. Mode A puts (ADDSET+1) HCLK before the RDX
    // pulse, (DATAST+3) HCLK behind it, so 6/3 gives 97.2 / 83.3 ns and a
    // 180.6 ns cycle at HCLK = 72 MHz.
    read_timing.AddressSetupTime = 0x06;
    read_timing.DataSetupTime = 0x03;
    read_timing.BusTurnAroundDuration = 0;
    read_timing.AccessMode = FSMC_ACCESS_MODE_A;

    // The panel spec (ST7789VW Table 4) wants a WRX cycle of at least 66 ns
    // with 15 ns pulses. In mode A the high time is ADDSET+1 HCLK and the low
    // time is DATAST+1 HCLK, so 1/2 is the smallest pair that fits: 27.8 ns
    // high, 41.7 ns low, 69.4 ns cycle at HCLK = 72 MHz.
    write_timing.AddressSetupTime = 0x01;
    write_timing.DataSetupTime = 0x02;
    write_timing.BusTurnAroundDuration = 0;
    write_timing.AccessMode = FSMC_ACCESS_MODE_A;

    HAL_SRAM_Init(&lcd_sram, &read_timing, &write_timing);
}

//............................................................................
static void BSP_lcdWriteCmd_(uint16_t cmd) {
    LCD_CMD = cmd;
}

//............................................................................
static void BSP_lcdWriteDat_(uint16_t dat) {
    LCD_DAT = dat;
}

//............................................................................
static uint16_t BSP_lcdReadDat_(void) {
    __NOP();
    __NOP();
    return LCD_DAT;
}

//............................................................................
static uint16_t BSP_lcdReadId_(void) {
    uint16_t id;

    BSP_lcdWriteCmd_(LCD_RDID_CMD);
    (void)BSP_lcdReadDat_();                          // dummy
    (void)BSP_lcdReadDat_();                          // dummy
    id = (uint16_t)(BSP_lcdReadDat_() << 8);
    id |= (uint16_t)(BSP_lcdReadDat_() & 0x00FFU);
    return id;
}
