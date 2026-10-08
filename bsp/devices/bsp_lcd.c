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
#define LCD_SWRESET_CMD 0x01U  // Stands in for the hardware reset this board
                               // cannot give the panel (see BSP_lcdInit).
#define LCD_SLPOUT_CMD  0x11U  // Frame memory is unreachable while asleep.
#define LCD_COLMOD_16BIT 0x05U // MCU interface colour format: 16 bit/pixel.
#define LCD_CASET_CMD   0x2AU
#define LCD_RASET_CMD   0x2BU
#define LCD_RAMWR_CMD   0x2CU
#define LCD_RAMRD_CMD   0x2EU
#define LCD_DISPON_CMD  0x29U  // Sent once the frame memory is cleared.
#define LCD_BLACK       0x0000U

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
// Software reset recovery: 5 ms before the next command, and 120 ms before
// SLPOUT when the reset lands while the panel is in sleep-in mode (that is
// what the datasheet says about 01h). 130 again, for margin.
#define LCD_SWRESET_SETTLE_MS 130U

// Bring-up is verified rather than assumed: the black fill is read back from
// both halves, and a bring-up that did not land is run again after a longer
// settle. On a cold start with no debugger the panel gets no clean hardware
// reset (RESX just follows the rail), so how long its own power-on sequence
// takes is not ours to know - the retries are what make the cold start
// deterministic instead of hopeful.
// The panel only leaves reset once RESX - tied to the board reset net - has
// risen far enough for its input to see a high level, and how long that takes
// is not a constant: measured 3018, 3194 and 3816 ms on different power-ons,
// and at least one that was still not ready after 4563 ms. Debugger attached
// makes it faster (the probe adds a ground path and an NRST pull-up), so the
// detached case is the one to size for. The budget below therefore covers the
// slow end with margin rather than matching any single measurement, and a
// board that is already up still leaves on the first pass.
#define LCD_INIT_ATTEMPTS    100U
#define LCD_RETRY_SETTLE_MS  100U
// Trace records: ==LCD==[RDID][<hi> <lo>]==[ST7789/??????]==
//                ==LCD==[DISPON]==
#define LCD_TRACE_RDID_OK   12U
#define LCD_TRACE_RDID_BAD  13U
#define LCD_TRACE_DISP_ON   20U
#define LCD_TRACE_GRAM      37U

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
    // MADCTL: BGR bit set. Measured, not inherited: with 0x00 the panel swaps
    // red and blue (a red/green/blue/white/black bar test came back
    // blue/green/red/white/black), which turns the teal backgrounds yellow.
    // The vendor's own ATK-MD0240 driver writes 0x00, so this module wants
    // the other order - the bars say so.
    {0x36U,  1U, {0x08U}},                                // MADCTL (BGR)
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
    {0x21U,  0U, {0x00U}}                                 // inversion on
    // Display on is not in here: BSP_lcdInit() clears the frame memory in
    // between, so the panel never scans an uninitialised GRAM (BSP_lcdFill_).
};

//============================================================================
static void BSP_lcdGpioInit_(void);
static void BSP_lcdFsmcInit_(void);
static void BSP_lcdWriteCmd_(uint16_t cmd);
static void BSP_lcdWriteDat_(uint16_t dat);
static uint16_t BSP_lcdReadDat_(void);
static uint16_t BSP_lcdReadId_(void);
static void BSP_lcdPanelInit_(void);
static void BSP_lcdFill_(uint16_t colour);
static uint16_t BSP_lcdReadGram_(uint16_t x, uint16_t y);
static bool BSP_lcdFilled_(void);
static void BSP_lcdTraceId_(void);

//============================================================================
void BSP_lcdInit(void) {
    uint8_t attempt;
    uint32_t const sinceReset = HAL_GetTick();

    BSP_lcdGpioInit_();
    BSP_lcdFsmcInit_();

    // The panel's RESX is tied to the board reset, so on a cold start it only
    // follows VDD up instead of being held low until the supply is stable.
    // The datasheet calls that out (Note 4, p.136-137): with no real reset
    // after power up the behaviour is not guaranteed, and the remedy it names
    // is a hardware reset once the supply is up - which this board cannot
    // give. Software reset is the stand-in, and the symptom of getting the
    // timing wrong is a frame memory write that only partly lands: half a
    // frame of noise, the rest black.
    // The scheduler is not up yet, so HAL_Delay() - which runs off the TIM1
    // HAL tick, alive since HAL_Init() - is the only wait available here.
    // A power-on needs far longer than this before the panel will hold frame
    // memory, and that wait is not served here: the retry loop below starts
    // early and keeps trying, so the boot costs the panel's real readiness
    // time rather than a constant that has to cover its worst case.
    if (sinceReset < LCD_RESET_SETTLE_MS) {
        HAL_Delay(LCD_RESET_SETTLE_MS - sinceReset);
    }

    for (attempt = 0U; attempt < LCD_INIT_ATTEMPTS; ++attempt) {
        // Sleep-out and the software reset go out once. Note 1 ties the NVM
        // load (ID bytes, VCOM, ...) to the reset path with tRT up to 120 ms,
        // so re-sending the reset on every pass only restarts that load. The
        // register table and the fill are what get retried.
        if (attempt == 0U) {
            BSP_lcdWriteCmd_(LCD_SWRESET_CMD);
            HAL_Delay(LCD_SWRESET_SETTLE_MS);

            BSP_lcdWriteCmd_(LCD_SLPOUT_CMD);
            HAL_Delay(LCD_SLPOUT_SETTLE_MS);
        }

        BSP_lcdPanelInit_();

        // Frame memory first, display on second: the panel starts scanning
        // GRAM the moment DISPON lands, and until this clear it holds
        // whatever the glass powered up with.
        BSP_lcdFill_(LCD_BLACK);

        if (BSP_lcdFilled_()) {
            break;
        }

        // The fill did not land, so the panel is not out of reset yet. Keep
        // the passes close together: the window between "not yet" and "now"
        // is only ~112 ms wide, and waiting longer between passes would just
        // miss it later.
        HAL_Delay(LCD_RETRY_SETTLE_MS);
    }

    BSP_lcdWriteCmd_(LCD_DISPON_CMD);

    // Backlight last, and only now: the frame memory is black and the panel
    // is out of its sleep sequence, so the glass comes on in a defined state.
    // The panel's own power-up noise, the retries above and the supply ramp
    // all happened behind a dark glass. From here the display layer owns the
    // drawing, and the UI thread paints its first frame onto this black.
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_SET);

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
// Window set: CASET + RASET + RAMWR. Parameters go out 8 bit on the low byte,
// which is what the panel expects in the 16-bit 8080 mode.
void BSP_lcdSetWindow(uint16_t xs, uint16_t ys,
                      uint16_t xe, uint16_t ye) {
    BSP_lcdWriteCmd_(LCD_CASET_CMD);
    BSP_lcdWriteDat_(xs >> 8U);
    BSP_lcdWriteDat_(xs & 0x00FFU);
    BSP_lcdWriteDat_(xe >> 8U);
    BSP_lcdWriteDat_(xe & 0x00FFU);

    BSP_lcdWriteCmd_(LCD_RASET_CMD);
    BSP_lcdWriteDat_(ys >> 8U);
    BSP_lcdWriteDat_(ys & 0x00FFU);
    BSP_lcdWriteDat_(ye >> 8U);
    BSP_lcdWriteDat_(ye & 0x00FFU);

    BSP_lcdWriteCmd_(LCD_RAMWR_CMD);
}
//............................................................................
// Fill the window just set. A whole 240x320 screen is 76,800 words, about
// 5.4 ms of bus time at the in-spec write timing - paid once per boot.
static void BSP_lcdFill_(uint16_t colour) {
    uint32_t px;

    BSP_lcdSetWindow(0U, 0U, LCD_WIDTH - 1U, LCD_HEIGHT - 1U);

    for (px = 0U; px < ((uint32_t)LCD_WIDTH * LCD_HEIGHT); ++px) {
        LCD_DAT = colour;  // straight to the bus: a call per pixel shows up
    }
}
//............................................................................
// Push pixels into the window BSP_lcdSetWindow() just opened. Same bus write
// as the fill above, so the per pixel cost is the same - the caller decides
// how many. The display layer's flush callback is the only user today.
void BSP_lcdWritePixels(uint16_t const *pixels, uint32_t count) {
    uint32_t i;

    for (i = 0U; i < count; ++i) {
        LCD_DAT = pixels[i];
    }
}

//............................................................................
//............................................................................
// Frame memory readback: narrow the window to one pixel, RAMRD, one dummy
// read (the first value back is not the pixel), then the pixel itself.
static uint16_t BSP_lcdReadGram_(uint16_t x, uint16_t y) {
    BSP_lcdSetWindow(x, y, x, y);
    BSP_lcdWriteCmd_(LCD_RAMRD_CMD);
    (void)BSP_lcdReadDat_();

    return BSP_lcdReadDat_();
}
//............................................................................
// Did the clear actually land? The same four points BSP_lcdCheck() reports
// later, spread over both halves of the frame: a fill that only reached part
// of the frame memory reads back as noise here, which is the cold start
// symptom this loop exists to catch.
static bool BSP_lcdFilled_(void) {
    static uint16_t const ys[4] = {0U, 159U, 160U, 319U};
    uint8_t i;

    for (i = 0U; i < 4U; ++i) {
        if (BSP_lcdReadGram_(0U, ys[i]) != LCD_BLACK) {
            return false;
        }
    }

    return true;
}
//............................................................................
// Sample both halves of the frame down the left edge. If the fill only
// landed in one of them these values say so, instead of leaving it to the
// eye. The AO calls this, not bring-up: a boot-time record is not observable
// from a terminal that cannot be attached until later (see AGENTS.md).
void BSP_lcdCheck(void) {
    static uint16_t const ys[4] = {0U, 159U, 160U, 319U};
    uint8_t i;

    for (i = 0U; i < 4U; ++i) {
        uint16_t const value = BSP_lcdReadGram_(0U, ys[i]);

        SM_TRACE_BEGIN(LCD_TRACE_GRAM, 5)
            SM_TRACE_PAYLOAD_BE(0U)
            SM_TRACE_PAYLOAD_BE(ys[i] >> 8)
            SM_TRACE_PAYLOAD_BE(ys[i])
            SM_TRACE_PAYLOAD_BE(value >> 8)
            SM_TRACE_PAYLOAD_BE(value)
        SM_TRACE_END()
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
    // Spec targets (ST7789VW Table 4): Read ID wants TRCS >= 45, TRDH >= 90,
    // TRDL >= 45, TRC >= 160; a frame memory read is much slower, TRCFM >=
    // 450. Mode A puts (ADDSET+1) HCLK before the RDX pulse and (DATAST+3)
    // HCLK behind it, so 6/23 gives 97.2 / 361 ns and a 458 ns cycle, which
    // is the one setting that covers both kinds of read.
    read_timing.AddressSetupTime = 0x06;
    read_timing.DataSetupTime = 0x17;
    read_timing.BusTurnAroundDuration = 0;
    read_timing.AccessMode = FSMC_ACCESS_MODE_A;

    // The panel spec (ST7789VW Table 4) wants a WRX cycle of at least 66 ns
    // with 15 ns pulses. In mode A the high time is ADDSET+1 HCLK and the low
    // time is DATAST+1 HCLK, so 1/2 is the smallest pair that fits the spec:
    // 27.8 ns high, 41.7 ns low, 69.4 ns cycle at HCLK = 72 MHz. Both pulses
    // are well over the 15 ns floor, so this meets the datasheet and is kept:
    // bus throughput matters here (a full-screen flush is 5.33 ms at this
    // pair against 7.46 ms at 1/4). Widening it was tried while chasing the
    // cold-start readiness and did not move it (3018 ms against 3194 ms,
    // inside the 112 ms probe spacing), so there is no evidence linking the
    // timing to that problem - only a guess that 5% looked thin.
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
