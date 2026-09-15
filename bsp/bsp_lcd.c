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
#define LCD_CASET_CMD   0x2AU
#define LCD_RASET_CMD   0x2BU
#define LCD_RAMWR_CMD   0x2CU
#define LCD_RAMRD_CMD   0x2EU
#define LCD_COLMOD_CMD  0x3AU
#define LCD_COLMOD_16BIT 0x05U // MCU interface colour format: 16 bit/pixel.

#define LCD_ID_ST7789   0x8552U

#define LCD_WIDTH       240U
#define LCD_HEIGHT      320U

// The round trip test paints one row of pixels; anything left over is GRAM.
#define LCD_PROBE_X     0U
#define LCD_PROBE_Y     0U
#define LCD_PROBE_N     4U

// Rows painted per beat once the panel is up, so the frame is refreshed in
// 20 beats and every beat only spends a fraction of a millisecond on it.
#define LCD_FILL_ROWS   16U

// SLPOUT, then the rest of the register bring-up one beat later, which covers
// the 120 ms the panel asks for between the two.
#define LCD_WAKE_BEATS  2U

#define LCD_BL_PORT     GPIOB
#define LCD_BL_PIN      GPIO_PIN_0

// Trace records: ==LCD==[RDID][<hi> <lo>]==[OK/??]==
#define LCD_TRACE_RDID_OK   12U
#define LCD_TRACE_RDID_BAD  13U
// ==LCD==[GRAM16][OK]==  /  ==LCD==[GRAM16][BAD][idx ..][w2 ..][w3 ..]==
#define LCD_TRACE_GRAM_OK   14U
#define LCD_TRACE_GRAM_BAD  15U
// ==LCD==[CYCCNT][<delta>]==
#define LCD_TRACE_CYCCNT    16U
// ==LCD==[DISPON]==
#define LCD_TRACE_DISP_ON   20U

//============================================================================
// Panel bring-up. The values are the vendor sequence for this module
// (ALIENTEK atk_md0240_reg_init), minus the sleep-out wait and the window
// setup, which the probe does on every access.
typedef struct {
    uint16_t cmd;
    uint8_t argCount;
    uint8_t args[14];
} BSP_lcdInitStep_;

static BSP_lcdInitStep_ const lcdInitSeq_[] = {
    {0x36U,  1U, {0x00U}},                                       // MADCTL
    {0x3AU,  1U, {LCD_COLMOD_16BIT}},                            // pixel format
    {0xB2U,  5U, {0x0CU, 0x0CU, 0x00U, 0x33U, 0x33U}},           // porch
    {0xB7U,  1U, {0x35U}},                                       // gate control
    {0xBBU,  1U, {0x32U}},                                       // VCOM
    {0xC0U,  1U, {0x0CU}},                                       // power control
    {0xC2U,  1U, {0x01U}},                                       // VDVRH enable
    {0xC3U,  1U, {0x10U}},                                       // VRH
    {0xC4U,  1U, {0x20U}},                                       // VDV
    {0xC6U,  1U, {0x0FU}},                                       // frame rate
    {0xD0U,  2U, {0xA4U, 0xA1U}},                                // power control 2
    {0xE0U, 14U, {0xD0U, 0x00U, 0x02U, 0x07U, 0x0AU, 0x28U, 0x32U,
                  0x44U, 0x42U, 0x06U, 0x0EU, 0x12U, 0x14U, 0x17U}},
    {0xE1U, 14U, {0xD0U, 0x00U, 0x02U, 0x07U, 0x0AU, 0x28U, 0x31U,
                  0x54U, 0x47U, 0x0EU, 0x1CU, 0x17U, 0x1BU, 0x1EU}},
    {0x21U,  0U, {0x00U}},                                       // inversion on
    {0x29U,  0U, {0x00U}}                                        // display on
};

//============================================================================
static void BSP_lcdGpioInit_(void);
static void BSP_lcdFsmcInit_(void);
static void BSP_lcdWriteCmd_(uint16_t cmd);
static void BSP_lcdWriteDat_(uint16_t dat);
static uint16_t BSP_lcdReadDat_(void);
static uint16_t BSP_lcdReadId_(void);
static void BSP_lcdPanelInit_(void);
static void BSP_lcdSetWindow_(uint16_t x0, uint16_t y0,
                              uint16_t x1, uint16_t y1);
static uint16_t BSP_lcdReadPoint_(uint16_t x, uint16_t y, uint16_t color,
                                  uint16_t * const w2, uint16_t * const w3);
static void BSP_lcdTraceId_(void);
static void BSP_lcdTraceGram_(void);
static void BSP_lcdTraceCyccnt_(void);
static void BSP_lcdFillBand_(void);

static uint16_t lcd_beats;
static uint16_t lcd_fillRow;
static uint8_t  lcd_fillColor;

//============================================================================
void BSP_lcdInit(void) {
    BSP_lcdGpioInit_();
    BSP_lcdFsmcInit_();

    // Backlight on, so the panel power path is observable on the bench.
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_SET);
}

//............................................................................
void BSP_lcdProbe(void) {
    ++lcd_beats;

    // Wake the panel so its frame memory becomes reachable, then finish the
    // register bring-up one beat later: one step per beat, so the 120 ms
    // SLPOUT wait costs nothing.
    if (lcd_beats == 1U) {
        BSP_lcdWriteCmd_(LCD_SLPOUT_CMD);
    } else if (lcd_beats == 2U) {
        BSP_lcdPanelInit_();
        SM_TRACE_BEGIN(LCD_TRACE_DISP_ON, 0)
        SM_TRACE_END()
    }

    BSP_lcdTraceId_();
    if (lcd_beats > LCD_WAKE_BEATS) {
        BSP_lcdTraceGram_();
        BSP_lcdFillBand_();
    }
    if ((lcd_beats % 8U) == 0U) {
        BSP_lcdTraceCyccnt_();
    }
}

//............................................................................
static void BSP_lcdPanelInit_(void) {
    uint8_t step;

    for (step = 0U; step < (uint8_t)(sizeof(lcdInitSeq_) / sizeof(lcdInitSeq_[0]));
         ++step) {
        uint8_t arg;

        BSP_lcdWriteCmd_(lcdInitSeq_[step].cmd);
        for (arg = 0U; arg < lcdInitSeq_[step].argCount; ++arg) {
            BSP_lcdWriteDat_(lcdInitSeq_[step].args[arg]);
        }
    }
}

//............................................................................
// Keep the write path busy and give the bench something to look at: one band
// of rows per beat, the colour changing every time the frame wraps.
static void BSP_lcdFillBand_(void) {
    static uint16_t const color[4] = {0xF800U, 0x07E0U, 0x001FU, 0xFFFFU};
    uint16_t const last = (uint16_t)(lcd_fillRow + LCD_FILL_ROWS - 1U);
    uint16_t const pixels = (uint16_t)(LCD_WIDTH * LCD_FILL_ROWS);
    uint16_t i;

    BSP_lcdSetWindow_(0U, lcd_fillRow, (uint16_t)(LCD_WIDTH - 1U), last);
    BSP_lcdWriteCmd_(LCD_RAMWR_CMD);
    for (i = 0U; i < pixels; ++i) {
        BSP_lcdWriteDat_(color[lcd_fillColor]);
    }

    lcd_fillRow = (uint16_t)(last + 1U);
    if (lcd_fillRow >= LCD_HEIGHT) {
        lcd_fillRow = 0U;
        lcd_fillColor = (uint8_t)((lcd_fillColor + 1U) & 0x03U);
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
// Write four patterns into the frame memory and read them back. The patterns
// cover every data line at both rails and in alternating pairs; the first
// mismatch is reported together with the raw words that produced it.
static void BSP_lcdTraceGram_(void) {
    static uint16_t const pattern[LCD_PROBE_N] = {
        0xFFFFU, 0x0000U, 0xAAAAU, 0x5555U
    };
    uint8_t i;

    for (i = 0U; i < LCD_PROBE_N; ++i) {
        uint16_t w2;
        uint16_t w3;
        uint16_t const back = BSP_lcdReadPoint_(
            (uint16_t)(LCD_PROBE_X + i), LCD_PROBE_Y, pattern[i], &w2, &w3);

        if (back != pattern[i]) {
            SM_TRACE_BEGIN(LCD_TRACE_GRAM_BAD, 5)
                SM_TRACE_PAYLOAD_BE(i)
                SM_TRACE_PAYLOAD_BE(w2 >> 8)
                SM_TRACE_PAYLOAD_BE(w2)
                SM_TRACE_PAYLOAD_BE(w3 >> 8)
                SM_TRACE_PAYLOAD_BE(w3)
            SM_TRACE_END()
            return;
        }
    }

    SM_TRACE_BEGIN(LCD_TRACE_GRAM_OK, 0)
    SM_TRACE_END()
}

//............................................................................
// The ledOn/ledOff timestamps come from DWT->CYCCNT. If that counter stops,
// they repeat; this reports the delta of one probe delay instead.
static void BSP_lcdTraceCyccnt_(void) {
    static uint32_t last;
    uint32_t const now = DWT->CYCCNT;
    uint32_t const delta = now - last;

    last = now;

    SM_TRACE_BEGIN(LCD_TRACE_CYCCNT, 4)
        SM_TRACE_PAYLOAD_BE(delta >> 24)
        SM_TRACE_PAYLOAD_BE(delta >> 16)
        SM_TRACE_PAYLOAD_BE(delta >> 8)
        SM_TRACE_PAYLOAD_BE(delta >> 0)
    SM_TRACE_END()
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

    // Mode A. The panel read path is slow, so give the read data phase room.
    // Write timing follows the vendor reference for this board + module.
    read_timing.AddressSetupTime = 0x00;
    read_timing.DataSetupTime = 0x0F;
    read_timing.BusTurnAroundDuration = 0;
    read_timing.AccessMode = FSMC_ACCESS_MODE_A;

    write_timing.AddressSetupTime = 0x00;
    write_timing.DataSetupTime = 0x01;
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

//............................................................................
// Window written the way the vendor driver writes parameters: high byte then
// low byte, for each end of each range.
static void BSP_lcdSetWindow_(uint16_t x0, uint16_t y0,
                              uint16_t x1, uint16_t y1) {
    BSP_lcdWriteCmd_(LCD_CASET_CMD);
    BSP_lcdWriteDat_((uint16_t)((x0 >> 8) & 0x00FFU));
    BSP_lcdWriteDat_((uint16_t)(x0 & 0x00FFU));
    BSP_lcdWriteDat_((uint16_t)((x1 >> 8) & 0x00FFU));
    BSP_lcdWriteDat_((uint16_t)(x1 & 0x00FFU));

    BSP_lcdWriteCmd_(LCD_RASET_CMD);
    BSP_lcdWriteDat_((uint16_t)((y0 >> 8) & 0x00FFU));
    BSP_lcdWriteDat_((uint16_t)(y0 & 0x00FFU));
    BSP_lcdWriteDat_((uint16_t)((y1 >> 8) & 0x00FFU));
    BSP_lcdWriteDat_((uint16_t)(y1 & 0x00FFU));
}

//............................................................................
// One pixel down the write path and back up the read path. The panel returns
// its 18 bit frame memory through two words, so the fields are packed back
// into RGB565 the same way the vendor driver does: D15..D11 carry R, D10..D5
// carry G and D4..D0 carry B, which is what makes the patterns a bus test.
static uint16_t BSP_lcdReadPoint_(uint16_t x, uint16_t y, uint16_t color,
                                  uint16_t * const w2, uint16_t * const w3) {
    BSP_lcdSetWindow_(x, y, x, y);
    BSP_lcdWriteCmd_(LCD_RAMWR_CMD);
    BSP_lcdWriteDat_(color);

    BSP_lcdSetWindow_(x, y, x, y);
    BSP_lcdWriteCmd_(LCD_RAMRD_CMD);
    (void)BSP_lcdReadDat_();                  // Dummy, as in the vendor driver.
    *w2 = BSP_lcdReadDat_();                  // [15:11] R, [7:2] G
    *w3 = BSP_lcdReadDat_();                  // [15:11] B

    return (uint16_t)((((*w2 >> 11) & 0x1FU) << 11)
                    | (((*w2 >>  2) & 0x3FU) <<  5)
                    | (((*w3 >> 11) & 0x1FU) <<  0));
}
