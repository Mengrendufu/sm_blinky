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
#include "sm_assert.h"
#include "sm_tracer.h"
#include "bsp_lcd.h"

//============================================================================
#define LCD_BANK_ADDR   0x6C000000U
#define LCD_CMD_ADDR    (LCD_BANK_ADDR | 0x7FEU)  // RS = FSMC_A10 = 0
#define LCD_DAT_ADDR    (LCD_BANK_ADDR | 0x800U)  // RS = FSMC_A10 = 1
#define LCD_CMD         (*(volatile uint16_t *)LCD_CMD_ADDR)
#define LCD_DAT         (*(volatile uint16_t *)LCD_DAT_ADDR)

#define LCD_RDID_CMD    0x04U
#define LCD_RDDST_CMD   0x09U
#define LCD_SWRESET_CMD 0x01U  // Stands in for the hardware reset this board
                               // cannot give the panel (see BSP_lcdInit).
#define LCD_SLPOUT_CMD  0x11U
#define LCD_SLPOUT_CMD  0x11U  // Frame memory is unreachable while asleep.
#define LCD_COLMOD_16BIT 0x05U // MCU interface colour format: 16 bit/pixel.
#define LCD_CASET_CMD   0x2AU
#define LCD_RASET_CMD   0x2BU
#define LCD_RAMWR_CMD   0x2CU
#define LCD_RAMRD_CMD   0x2EU
#define LCD_INVON_CMD   0x21U  // display inversion: the black-level polarity
#define LCD_DISPON_CMD  0x29U  // Sent once the frame memory is cleared.
#define LCD_BLACK       0x0000U

#define LCD_ID_ST7789   0x8552U

// Panel geometry; the display layer will address the panel through these.
#define LCD_WIDTH       240U
#define LCD_HEIGHT      320U

#define LCD_BL_PORT     GPIOB
#define LCD_BL_PIN      GPIO_PIN_0

// The strip push runs on DMA1 channel 6, which is what charter §3 reserved
// for the flush: SPI2 owns channels 4/5 (the flash), channel 1 stays free for
// ADC1, 2/3 are held for a second SPI, and 7 stays spare. M2M is not a
// preference here, it is the only mode that fits - neither endpoint is a
// handshake peripheral (bsp_lcd.h).
#define LCD_DMA_CHANNEL  DMA1_Channel6
#define LCD_DMA_IRQN     DMA1_Channel6_IRQn
// 5 is configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (FreeRTOSConfig.h:123).
// This interrupt wakes a task, and a numerically lower priority would make
// that call illegal in a way nothing reports at run time.
#define LCD_DMA_IRQ_PRIO 5U

// ST7789VW reset timing: commands may start 5 ms after RESX/SWRESET;
// SLPOUT must wait 120 ms after reset if the panel was asleep. After
// SLPOUT, wait 5 ms before commands; the separate 120 ms rule forbids SLPIN,
// not communication or GRAM testing. See docs/lcd-init.md.
#define LCD_RESET_SETTLE_MS    5U
#define LCD_SWRESET_SETTLE_MS  5U
#define LCD_SLPOUT_AFTER_RESET_MS 120U
#define LCD_SLPOUT_SETTLE_MS   5U

// Probe before scheduling. ID establishes communication; only both X
// patterns establish sampled writability. Neither probe restarts SWRESET.
#define LCD_INIT_ATTEMPTS    1100U
#define LCD_READY_POLL_MS    10U
#define LCD_RETRY_SETTLE_MS  20U
// Trace records: ==LCD==[RDID][<hi> <lo>]==[ST7789/??????]==
//                ==LCD==[DISPON]==
#define LCD_TRACE_RDID_OK   12U
#define LCD_TRACE_RDID_BAD  13U
#define LCD_TRACE_DISP_ON   20U
#define LCD_TRACE_INIT_MS 103U
#define LCD_TRACE_INIT_ATTEMPTS 104U
#define LCD_TRACE_READY_FAILURES 105U
#define LCD_TRACE_WRITE_FAILURES 106U
// Trace 107 (clear-check failures) retired; do not reuse.
#define LCD_TRACE_INIT_VERIFIED 108U
#define LCD_TRACE_COMM_FAILURES 109U

// The bring-up facts are kept here instead of pushed: the tracer comes up
// with the first task, and a push before the scheduler enters a FreeRTOS
// critical section whose nesting counter still holds its sentinel - which
// masks interrupts for the rest of the boot (and would hang the retry loop's
// HAL_Delay in BSP_lcdInit).
static bool lcd_dbgBootDispOn_;
static uint16_t lcd_dbgBootId_;
// == DEBUG: boot-only facts, published after scheduler startup. ==
static struct {
    uint32_t elapsedMs;
    uint32_t attempts;
    uint32_t readyFailures;
    uint32_t writeFailures;
    uint32_t verified;
    uint32_t commFailures;
} lcd_dbgBoot_;




//============================================================================
// Panel register sequence: the vendor table for this module (ALIENTEK
// atk_md0240_reg_init). SLPOUT is not part of it; BSP_lcdInit() sends that
// first, then waits the panel's sleep-out time out.
typedef struct {
    uint16_t cmd;
    uint8_t argCount;
    uint8_t args[14];
} BSP_lcdInitStep_;

static DMA_HandleTypeDef lcdDma_;
static BSP_Lcd lcdDevice_ = {.device = &lcdDma_};
static volatile bool lcdDmaBusy_;
static BSP_lcdDone lcdDmaDone_;
static void *lcdDmaContext_;
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
    {0x21U,  0U, {0x00U}}                  // inversion on (black polarity)
    // Display on is not in here: BSP_lcdInit() clears the frame memory in
    // between, so the panel never scans an uninitialised GRAM (BSP_lcdFill_).
};

//============================================================================
static void BSP_lcdWaitSince_(uint32_t since, uint32_t minimum);
static void BSP_lcdGpioInit_(void);
static void BSP_lcdFsmcInit_(void);
static void BSP_lcdDmaInit_(void);

static void BSP_lcdSetWindow_(uint16_t xs, uint16_t ys,
                               uint16_t xe, uint16_t ye);
static void BSP_lcdDmaCplt_(DMA_HandleTypeDef *hdma);
static void BSP_lcdDmaError_(DMA_HandleTypeDef *hdma);
static void BSP_lcdWriteCmd_(uint16_t cmd);
static void BSP_lcdWriteDat_(uint16_t dat);
static uint16_t BSP_lcdReadDat_(void);
static uint16_t BSP_lcdReadId_(void);
static void BSP_lcdPanelInit_(void);
static void BSP_lcdFill_(uint16_t colour);
static uint16_t BSP_lcdReadGram_(uint16_t x, uint16_t y);
static void BSP_lcdReadRdst_(uint8_t out[4]);
static bool BSP_lcdReady_(void);
static void BSP_lcdWritePixel_(uint16_t x, uint16_t y, uint16_t colour);
static uint16_t lcdSentinelX_(uint16_t row, unsigned diagonal);
static uint16_t lcdSentinelColour_(uint16_t row, unsigned diagonal,
                                    unsigned phase);
static void BSP_lcdSentinelWrite_(unsigned phase);
static bool BSP_lcdSentinelAll_(unsigned phase);
static bool BSP_lcdWritable_(void);
static void BSP_dbgLcdBootId_(void);

// Two diagonals, two points per row: 640 positions on this 240x320 panel.
// Magenta and green are RGB565 complements and invariant under R/B swap.
// Swap both colours on the second pass to reject stale and constant reads.
#define LCD_SENTINEL_A 0xF81FU
#define LCD_SENTINEL_B 0x07E0U
#define LCD_COLMOD_CMD 0x3AU
#define LCD_COLMOD_READ 0x66U

//============================================================================
void BSP_lcdInit(void) {
    uint16_t attempt;
    uint32_t const sinceReset = HAL_GetTick();
    lcd_dbgBoot_.elapsedMs = 0U;
    lcd_dbgBoot_.attempts = 0U;
    lcd_dbgBoot_.readyFailures = 0U;
    lcd_dbgBoot_.writeFailures = 0U;
    lcd_dbgBoot_.verified = 0U;
    lcd_dbgBoot_.commFailures = 0U;
    lcd_dbgBootDispOn_ = false;

    BSP_lcdGpioInit_();
    BSP_lcdFsmcInit_();
    // The channel comes up with the bus it serves. Nothing is armed until the
    // display layer asks, and the wait capabilities are bound later by the
    // UI thread that will do the waiting.
    BSP_lcdDmaInit_();

    // HAL tick starts after reset release, so waiting from epoch zero is
    // conservative. Do not stack another full reset-settle delay here.
    BSP_lcdWaitSince_(0U, LCD_RESET_SETTLE_MS);
    BSP_lcdWriteCmd_(LCD_SWRESET_CMD);
    uint32_t const resetAt = HAL_GetTick();
    BSP_lcdWaitSince_(resetAt, LCD_SWRESET_SETTLE_MS);

    uint16_t id = 0U;
    // The bus has no ACK: an expected ID is the communication criterion.
    // Spend the mandatory pre-SLPOUT interval probing rather than starting
    // that interval again after the ID arrives. No further stage on timeout.
    for (attempt = 0U; attempt < LCD_INIT_ATTEMPTS; ++attempt) {
        id = BSP_lcdReadId_();
        if (id == LCD_ID_ST7789) {
            break;
        }
        ++lcd_dbgBoot_.commFailures;
        HAL_Delay(LCD_READY_POLL_MS);
    }
    lcd_dbgBootId_ = id;
    if (attempt == LCD_INIT_ATTEMPTS) {
        lcd_dbgBoot_.elapsedMs = HAL_GetTick() - sinceReset;
        return;
    }

    BSP_lcdWaitSince_(resetAt, LCD_SLPOUT_AFTER_RESET_MS);
    BSP_lcdWriteCmd_(LCD_SLPOUT_CMD);
    HAL_Delay(LCD_SLPOUT_SETTLE_MS);

    for (attempt = 0U; attempt < LCD_INIT_ATTEMPTS; ++attempt) {
        ++lcd_dbgBoot_.attempts;
        // Preserve the configured panel state used by the status check.
        BSP_lcdPanelInit_();
        // Sleep-out is only a cheap first gate. A ready register value
        // does not establish that frame-memory writes land on the panel.
        if (!BSP_lcdReady_()) {
            ++lcd_dbgBoot_.readyFailures;
            // The chip is asleep. Sleep out is idempotent and costs one
            // command, so ask again instead of waiting out the budget for a
            // state nothing will change: the one sleep out sent before the
            // loop is not guaranteed to have survived the chip's own power on
            // sequence, which can restart it in sleep in.
            BSP_lcdWriteCmd_(LCD_SLPOUT_CMD);
            HAL_Delay(LCD_READY_POLL_MS);
            continue;
        }

        // No register-table rewrite after this verification.
        if (!BSP_lcdWritable_()) {
            ++lcd_dbgBoot_.writeFailures;
            HAL_Delay(LCD_RETRY_SETTLE_MS);
            continue;
        }

        lcd_dbgBoot_.verified = 1U;
        // Remove the test pattern and prepare the background. Writability
        // was established above; this fill deliberately has no readback.
        BSP_lcdFill_(LCD_BLACK);
        break;
    }

    BSP_lcdWriteCmd_(LCD_DISPON_CMD);

    // Bring-up retries and GRAM checks finish with backlight off.
    // The UI HSM enables it on Splash entry, before that page is rendered;
    // flush completion does not control backlight or prove frame coverage.
    lcd_dbgBootDispOn_ = true;

    // One identification read: the "panel is talking" line on the bench.
    BSP_dbgLcdBootId_();
    lcd_dbgBoot_.elapsedMs = HAL_GetTick() - sinceReset;
}

//............................................................................
// Wait only the outstanding part of a mandatory command interval.
static void BSP_lcdWaitSince_(uint32_t since, uint32_t minimum) {
    uint32_t const elapsed = HAL_GetTick() - since;
    if (elapsed < minimum) {
        HAL_Delay(minimum - elapsed);
    }
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
// Shared by startup GRAM checks and the object's complete area operation.
//............................................................................
//............................................................................
// The run-time coverage proof used to live here: clear the frame and prove
// it, lay the sentinel, draw, then ask whether the drawing covered the frame
// (BSP_lcdClearFrame / BSP_lcdFrameArm / BSP_lcdFrameCovered). It is gone as
// of 2026-10-12. A sentinel is a picture on the glass, and proving a page's
// frame that way drew six bright lines across the screen at every page
// change, on a lit panel. The sentinel stays a bring-up instrument now, in
// BSP_lcdWritable_ below, where the backlight is still off. Run-time frames
// are trusted; lcd_dbgBoot_.verified records whether the sampled boot
// writability checks passed before initialization returned.
//............................................................................
static void BSP_lcdSetWindow_(uint16_t xs, uint16_t ys,
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

    BSP_lcdSetWindow_(0U, 0U, LCD_WIDTH - 1U, LCD_HEIGHT - 1U);

    for (px = 0U; px < ((uint32_t)LCD_WIDTH * LCD_HEIGHT); ++px) {
        LCD_DAT = colour;  // straight to the bus: a call per pixel shows up
    }
}
//............................................................................
// DMA bring-up: clock, channel, callbacks, interrupt line. Channel flags
// are cleared when a transfer is armed - DMA_SetConfig() does it
// (stm32f1xx_hal_dma.c:861) - which is the "false completion" hazard charter
// §3 calls out for the flash path; on the HAL path it is already covered.
static void BSP_lcdDmaInit_(void) {
    __HAL_RCC_DMA1_CLK_ENABLE();

    lcdDma_.Instance = LCD_DMA_CHANNEL;
    // M2M puts the source in CPAR and the destination in CMAR.
    lcdDma_.Init.Direction = DMA_MEMORY_TO_MEMORY;
    // M2M puts the source in CPAR and the destination in CMAR
    // (stm32f1xx_hal_dma.c:858-884), so PINC walks the strip - and the
    // destination must NOT walk: the panel's RS is FSMC_A10, which on a 16
    // bit bank is HADDR[11], so an incrementing destination flips RS to a
    // command
    // every 0x800 bytes, that is every 1024 pixels. A strip is 7680 px, so
    // the window registers get fed pixel values and the glass falls apart.
    // Every access has to land on the same register, as the CPU loop did.
    lcdDma_.Init.PeriphInc = DMA_PINC_ENABLE;
    lcdDma_.Init.MemInc = DMA_MINC_DISABLE;
    lcdDma_.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    lcdDma_.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    lcdDma_.Init.Mode = DMA_NORMAL;
    // Above the flash's channel 4/5: the flush has a deadline on the glass,
    // the flash read is bulk traffic (charter §3).
    lcdDma_.Init.Priority = DMA_PRIORITY_HIGH;

    if (HAL_DMA_Init(&lcdDma_) != HAL_OK) {
        SM_onAssert("lcd", __LINE__);
    }
    if (HAL_DMA_RegisterCallback(&lcdDma_, HAL_DMA_XFER_CPLT_CB_ID,
                                 BSP_lcdDmaCplt_) != HAL_OK) {
        SM_onAssert("lcd", __LINE__);
    }
    if (HAL_DMA_RegisterCallback(&lcdDma_, HAL_DMA_XFER_ERROR_CB_ID,
                                 BSP_lcdDmaError_) != HAL_OK) {
        SM_onAssert("lcd", __LINE__);
    }

    HAL_NVIC_SetPriority(LCD_DMA_IRQN, LCD_DMA_IRQ_PRIO, 0U);
    HAL_NVIC_EnableIRQ(LCD_DMA_IRQN);
}
//............................................................................
// RAMRD uses RGB666 even though normal writes use RGB565. In 16-bit
// MDT=00 packing, the first word contains R/G and the next starts with B.
// Discard the dummy, consume both words, and compare normalized RGB565.
// The caller selects COLMOD=66h for reading and restores 05h for writing.
static uint16_t BSP_lcdReadGram_(uint16_t x, uint16_t y) {
    BSP_lcdSetWindow_(x, y, x, y);
    BSP_lcdWriteCmd_(LCD_RAMRD_CMD);
    (void)BSP_lcdReadDat_();
    uint16_t const rg = BSP_lcdReadDat_();
    uint16_t const b = BSP_lcdReadDat_();
    return (uint16_t)((rg & 0xF800U) | ((rg & 0x00FCU) << 3U)
                      | ((b >> 11U) & 0x001FU));
}
//............................................................................
static uint16_t lcdSentinelX_(uint16_t row, unsigned diagonal) {
    uint16_t const x = (uint16_t)(((uint32_t)row * (LCD_WIDTH - 1U))
                                  / (LCD_HEIGHT - 1U));
    return diagonal == 0U ? x : (uint16_t)(LCD_WIDTH - 1U - x);
}

static uint16_t lcdSentinelColour_(uint16_t row, unsigned diagonal,
                                    unsigned phase) {
    return ((row + diagonal + phase) & 1U) != 0U
        ? LCD_SENTINEL_A : LCD_SENTINEL_B;
}

static void BSP_lcdSentinelWrite_(unsigned phase) {
    for (uint16_t row = 0U; row < LCD_HEIGHT; ++row) {
        for (unsigned diagonal = 0U; diagonal < 2U; ++diagonal) {
            BSP_lcdWritePixel_(lcdSentinelX_(row, diagonal), row,
                lcdSentinelColour_(row, diagonal, phase));
        }
    }
}

static bool BSP_lcdSentinelAll_(unsigned phase) {
    bool matched = true;
    BSP_lcdWriteCmd_(LCD_COLMOD_CMD);
    BSP_lcdWriteDat_(LCD_COLMOD_READ);
    for (uint16_t row = 0U; row < LCD_HEIGHT && matched; ++row) {
        for (unsigned diagonal = 0U; diagonal < 2U; ++diagonal) {
            uint16_t const value =
                BSP_lcdReadGram_(lcdSentinelX_(row, diagonal), row);
            if (value != lcdSentinelColour_(row, diagonal, phase)) {
                matched = false;
                break;
            }
        }
    }
    // Restore even after mismatch: retry and final fill both write RGB565.
    BSP_lcdWriteCmd_(LCD_COLMOD_CMD);
    BSP_lcdWriteDat_(LCD_COLMOD_16BIT);
    return matched;
}
//............................................................................
// One pixel, as its own little window: the panel's frame memory is addressed
// through CASET/RASET, so a single point is a window of one.
static void BSP_lcdWritePixel_(uint16_t x, uint16_t y, uint16_t colour) {
    BSP_lcdSetWindow_(x, y, x, y);
    BSP_lcdWriteDat_(colour);

}
//............................................................................
// Each position must accept both complementary colours in sequence.
static bool BSP_lcdWritable_(void) {
    for (unsigned phase = 0U; phase < 2U; ++phase) {
        BSP_lcdSentinelWrite_(phase);
        if (!BSP_lcdSentinelAll_(phase)) {
            return false;
        }
    }
    return true;
}
//............................................................................
static void BSP_dbgLcdBootId_(void) {
    lcd_dbgBootId_ = BSP_lcdReadId_();
}
//............................................................................
// Full-width boot values; a DISPON command is not proof of verification.
static void BSP_dbgLcdValue_(uint8_t record, uint32_t value) {
    SM_TRACE_BEGIN(record, 4)
        SM_TRACE_PAYLOAD_BE(value >> 24)
        SM_TRACE_PAYLOAD_BE(value >> 16)
        SM_TRACE_PAYLOAD_BE(value >> 8)
        SM_TRACE_PAYLOAD_BE(value)
    SM_TRACE_END(0)
}

// Report once from the Flash AO, after the scheduler has started.
void BSP_dbgLcdBootReport(void) {
    BSP_dbgLcdValue_(LCD_TRACE_INIT_MS, lcd_dbgBoot_.elapsedMs);
    BSP_dbgLcdValue_(LCD_TRACE_INIT_ATTEMPTS, lcd_dbgBoot_.attempts);
    BSP_dbgLcdValue_(LCD_TRACE_READY_FAILURES, lcd_dbgBoot_.readyFailures);
    BSP_dbgLcdValue_(LCD_TRACE_WRITE_FAILURES, lcd_dbgBoot_.writeFailures);
    BSP_dbgLcdValue_(LCD_TRACE_INIT_VERIFIED, lcd_dbgBoot_.verified);
    BSP_dbgLcdValue_(LCD_TRACE_COMM_FAILURES, lcd_dbgBoot_.commFailures);
    if (lcd_dbgBootDispOn_) {
        SM_TRACE_BEGIN(LCD_TRACE_DISP_ON, 0)
        SM_TRACE_END()
    }

    if (lcd_dbgBootId_ == LCD_ID_ST7789) {
        SM_TRACE_BEGIN(LCD_TRACE_RDID_OK, 2)
            SM_TRACE_PAYLOAD_BE(lcd_dbgBootId_ >> 8)
            SM_TRACE_PAYLOAD_BE(lcd_dbgBootId_)
        SM_TRACE_END()
    } else {
        SM_TRACE_BEGIN(LCD_TRACE_RDID_BAD, 2)
            SM_TRACE_PAYLOAD_BE(lcd_dbgBootId_ >> 8)
            SM_TRACE_PAYLOAD_BE(lcd_dbgBootId_)
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
    // time is DATAST+1 HCLK, so 1/2 was the smallest pair that fits the spec:
    // 27.8 ns high, 41.7 ns low, 69.4 ns cycle at HCLK = 72 MHz - 5% of
    // margin on the cycle, which is thin for a part whose own timing moves
    // with supply and temperature.
    //
    // 1/2 it is: 27.8 / 41.7 / 69.4 ns, 5% of margin on the cycle. 1/4 was
    // tried (97.2 ns, 47% of margin) against the cold start and changed
    // nothing measurable - the readiness mask stayed at exactly 0x7F and the
    // refusing pass count was 131 against 155 - so the thin margin is not the
    // problem and the faster pair is kept.
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
// 09h RDDST: the display's own status after its sleep out sequence. Same read
// shape as the identification above, four bytes instead of two.
//
// Measured on this panel (two boots, identical): asleep it answers
// 00 61 00 00 and a frame memory fill does not land; once ready it answers
// 00 53 00 20 and the fill lands. The bit that separates the two is byte 3,
// bit 5 - so that is what the bring-up loop waits on. It costs four register
// reads instead of a fill and a four point read back.
static void BSP_lcdReadRdst_(uint8_t out[4]) {
    uint16_t hi;
    uint16_t lo;

    BSP_lcdWriteCmd_(LCD_RDDST_CMD);
    (void)BSP_lcdReadDat_();                          // dummy
    (void)BSP_lcdReadDat_();                          // dummy
    hi = BSP_lcdReadDat_();
    lo = BSP_lcdReadDat_();
    out[0] = (uint8_t)(hi >> 8);
    out[1] = (uint8_t)hi;
    out[2] = (uint8_t)(lo >> 8);
    out[3] = (uint8_t)lo;
}
//............................................................................
// True once the panel says its frame memory is reachable.
static bool BSP_lcdReady_(void) {
    uint8_t rdst[4];

    BSP_lcdReadRdst_(rdst);

    return ((rdst[3] & 0x20U) != 0U);
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

//============================================================================
// POLLING OPERATIONS -- CPU writes to the fixed FSMC data register.
SM_DEFINE_MODULE("lcd")

#define LCD_TRANSFER_WAIT_MS 20U
#define LCD_DMA_MAX_UNITS 65535U

static void BSP_lcdWritePixelsPoll_(void *device, uint16_t const *pixels,
                                    uint32_t count, BSP_lcdDone done,
                                    void *context) {
    SM_REQUIRE(device == &lcdDma_);
    SM_REQUIRE(lcdDevice_.busy);
    for (uint32_t i = 0U; i < count; ++i) {
        BSP_lcdWriteDat_(pixels[i]);
    }
    done(context, true);
}

BSP_lcdOperations const BSP_lcdPollOperations = {
    .writePixels = &BSP_lcdWritePixelsPoll_,
};

//============================================================================
// DMA OPERATIONS -- fixed destination, incrementing source, one arm per call.
// A word carries two RGB565 pixels, low halfword first. Odd counts and
// halfword-only aligned sources retain the original halfword fallback.
static void BSP_lcdDmaFinish_(bool ok) {
    BSP_lcdDone const done = lcdDmaDone_;
    void * const context = lcdDmaContext_;

    SM_REQUIRE(lcdDmaBusy_);
    lcdDmaDone_ = (BSP_lcdDone)0;
    lcdDmaContext_ = (void *)0;
    lcdDmaBusy_ = false;
    done(context, ok);
}

static void BSP_lcdWritePixelsDma_(void *device, uint16_t const *pixels,
                                   uint32_t count, BSP_lcdDone done,
                                   void *context) {
    uint32_t transfers = count;
    uint32_t size = DMA_PDATAALIGN_HALFWORD;

    SM_REQUIRE(device == &lcdDma_);
    SM_REQUIRE(lcdDevice_.busy && !lcdDmaBusy_);
    SM_REQUIRE(lcdDma_.State == HAL_DMA_STATE_READY);
    if (((count & 1U) == 0U) && (((uintptr_t)pixels & 3U) == 0U)) {
        size = DMA_PDATAALIGN_WORD;
        transfers = count / 2U;
    }
    SM_REQUIRE((transfers > 0U) && (transfers <= LCD_DMA_MAX_UNITS));
    lcdDmaDone_ = done;
    lcdDmaContext_ = context;
    lcdDmaBusy_ = true;

    // HAL start does not change width bits. PINC walks CPAR (the source);
    // CMAR stays on LCD_DAT_ADDR so FSMC RS never flips into command mode.
    MODIFY_REG(lcdDma_.Instance->CCR, DMA_CCR_PSIZE | DMA_CCR_MSIZE,
               size | (size << 2U));
    // Publish borrowed pixel stores before the channel starts reading them.
    __DSB();
    if (HAL_DMA_Start_IT(&lcdDma_, (uint32_t)(uintptr_t)pixels,
                         LCD_DAT_ADDR, transfers) != HAL_OK) {
        BSP_lcdDmaFinish_(false);
    }
}

BSP_lcdOperations const BSP_lcdDmaOperations = {
    .writePixels = &BSP_lcdWritePixelsDma_,
};

// HAL has cleared flags, disabled interrupts and restored READY first.
static void BSP_lcdDmaCplt_(DMA_HandleTypeDef *hdma) {
    SM_REQUIRE(hdma == &lcdDma_);
    BSP_lcdDmaFinish_(true);
}

static void BSP_lcdDmaError_(DMA_HandleTypeDef *hdma) {
    SM_REQUIRE(hdma == &lcdDma_);
    BSP_lcdDmaFinish_(false);
}

// Keep the vector with the panel: bootloader does not link this driver.
void DMA1_Channel6_IRQHandler(void) {
    HAL_DMA_IRQHandler(&lcdDma_);
}

//============================================================================
// OBJECT BINDING AND PUBLIC METHODS -- window and pixels are indivisible.
BSP_Lcd *BSP_lcdDevice(void) {
    return &lcdDevice_;
}

void BSP_lcdBind(BSP_Lcd *me, BSP_lcdOperations const *operations,
                 BSP_lcdWaitOperations const *waitOperations,
                 void *waitContext) {
    SM_REQUIRE(me == &lcdDevice_);
    SM_REQUIRE(__get_IPSR() == 0U);
    SM_REQUIRE(!me->busy && !me->complete && !lcdDmaBusy_);
    SM_REQUIRE(lcdDma_.State == HAL_DMA_STATE_READY);
    SM_REQUIRE(operations != (BSP_lcdOperations const *)0);
    SM_REQUIRE(operations->writePixels != 0);
    SM_REQUIRE(waitOperations != (BSP_lcdWaitOperations const *)0);
    SM_REQUIRE(waitOperations->isCaller != 0);
    SM_REQUIRE(waitOperations->waitDone != 0);
    SM_REQUIRE(waitOperations->notifyDone != 0);
    SM_REQUIRE(waitOperations->yield != 0);
    if (me->waitOperations != (BSP_lcdWaitOperations const *)0) {
        SM_REQUIRE(me->waitOperations->isCaller(me->waitContext));
    }
    me->operations = operations;
    me->waitOperations = waitOperations;
    me->waitContext = waitContext;
}

static void BSP_lcdEnter_(BSP_Lcd *me) {
    SM_REQUIRE(me == &lcdDevice_);
    SM_REQUIRE(me->operations != (BSP_lcdOperations const *)0);
    SM_REQUIRE(me->waitOperations != (BSP_lcdWaitOperations const *)0);
    SM_REQUIRE(me->waitOperations->isCaller(me->waitContext));
    SM_REQUIRE(!me->busy && !me->complete && !lcdDmaBusy_);
    SM_REQUIRE(lcdDma_.State == HAL_DMA_STATE_READY);
    me->busy = true;
}

static void BSP_lcdDone_(void *context, bool ok) {
    BSP_Lcd * const me = context;

    SM_REQUIRE(me->busy && !me->complete);
    me->ok = ok;
    me->complete = true;
    me->waitOperations->notifyDone(me->waitContext);
}

static bool BSP_lcdWaitTransfer_(BSP_Lcd *me) {
    if (!me->waitOperations->waitDone(me->waitContext,
                                      LCD_TRANSFER_WAIT_MS)) {
        SM_onAssert("lcdma", __LINE__);
        return false; // Never release a buffer while DMA may own it.
    }
    SM_REQUIRE(me->complete && !lcdDmaBusy_);
    SM_REQUIRE(lcdDma_.State == HAL_DMA_STATE_READY);
    bool const ok = me->ok;

    me->complete = false;
    return ok;
}

bool BSP_lcdWriteArea(BSP_Lcd *me, uint16_t xs, uint16_t ys, uint16_t xe,
                      uint16_t ye, uint16_t const *pixels) {
    BSP_lcdEnter_(me);
    SM_REQUIRE((xs <= xe) && (ys <= ye));
    SM_REQUIRE((xe < LCD_WIDTH) && (ye < LCD_HEIGHT));
    SM_REQUIRE(pixels != (uint16_t const *)0);
    SM_REQUIRE(((uintptr_t)pixels & 1U) == 0U);
    uint32_t left = ((uint32_t)xe - xs + 1U) * ((uint32_t)ye - ys + 1U);
    bool ok = true;

    BSP_lcdSetWindow_(xs, ys, xe, ye);
    while (ok && (left > 0U)) {
        uint32_t n = left;

        if (n > LCD_DMA_MAX_UNITS) {
            // Large aligned/even spans keep word DMA. A halfword-aligned
            // span uses one bounded chunk, then continues the same window.
            n = (((uintptr_t)pixels & 3U) == 0U)
                ? ((n > 2U * LCD_DMA_MAX_UNITS)
                   ? 2U * LCD_DMA_MAX_UNITS : (n & ~1U))
                : LCD_DMA_MAX_UNITS;
        }
        me->operations->writePixels(me->device, pixels, n,
                                     &BSP_lcdDone_, me);
        ok = BSP_lcdWaitTransfer_(me);
        pixels += n;
        left -= n;
        if (ok && (left > 0U)) {
            me->waitOperations->yield(me->waitContext);
        }
    }
    me->busy = false;
    return ok;
}

void BSP_lcdBacklight(BSP_Lcd *me, bool on) {
    BSP_lcdEnter_(me);
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    me->busy = false;
}

// Put the panel's inversion back. A panel that is still running its own
// power-up can end that sequence by loading its NVM, and what that leaves
// behind is a panel whose black level has the wrong polarity: black comes up
// as a bright field. The inversion bit is what that level hangs on, and
// unlike the power and VCOM registers it can be written while the panel is
// scanning - idempotent, tens of microseconds.
//
// This was taken out once, as an unproven hedge, and put back the same day:
// with it gone the glass came up inverted and every region drawn afterwards
// carried the wrong polarity - garbage wherever the picture changed, which is
// exactly what it looked like on the bench (2026-10-12). It is now re-sent
// forever, slowly, because that clobber has no schedule of its own.
void BSP_lcdReassert(BSP_Lcd *me) {
    BSP_lcdEnter_(me);
    BSP_lcdWriteCmd_(LCD_INVON_CMD);
    me->busy = false;
}
