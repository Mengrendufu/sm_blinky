#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint16_t pixels[320][240];
static uint16_t column, row, command;
static unsigned format, reads, writes, readCycle, fills, tables;
static unsigned mode, readyMisses, sleepOuts;
static unsigned commMisses, idReads, resetCommands;
static uint32_t resetAt, sleepAt;
static bool forceNoId;
static uint32_t tick;
static bool forceSleep;
enum { GOOD, NO_WRITES, SECOND_PHASE_STUCK, BAD_ROW, ALIAS, STUCK_READ };

static void BSP_lcdSetWindow_(uint16_t x, uint16_t y,
                              uint16_t xe, uint16_t ye);
static void BSP_lcdWriteCmd_(uint16_t value);
static void BSP_lcdWriteDat_(uint16_t value);
static uint16_t BSP_lcdReadDat_(void);
static void BSP_lcdGpioInit_(void) {}
static void BSP_lcdFsmcInit_(void) {}
static void BSP_lcdDmaInit_(void) {}
static void BSP_lcdPanelInit_(void);
static bool BSP_lcdReady_(void);
static void BSP_lcdFill_(uint16_t colour);
static void BSP_dbgLcdBootId_(void);
static uint16_t BSP_lcdReadId_(void);
static uint32_t HAL_GetTick(void) { return tick; }
static void HAL_Delay(uint32_t ms) { tick += ms; }

#include "production.h"

static void BSP_lcdSetWindow_(uint16_t x, uint16_t y,
                              uint16_t xe, uint16_t ye) {
    assert(x == xe && y == ye && x < 240U && y < 320U);
    column = mode == ALIAS ? 0U : x;
    row = y;
    command = LCD_RAMWR_CMD;
}
static void BSP_lcdWriteCmd_(uint16_t value) {
    command = value;
    readCycle = 0U;
    if (value == LCD_SWRESET_CMD) {
        assert(tick >= LCD_RESET_SETTLE_MS);
        ++resetCommands;
        resetAt = tick;
    }
    if (value == LCD_SLPOUT_CMD) {
        assert(tick - resetAt >= LCD_SLPOUT_AFTER_RESET_MS);
        ++sleepOuts;
        sleepAt = tick;
    }
}
static void BSP_lcdWriteDat_(uint16_t value) {
    if (command == LCD_COLMOD_CMD) { format = value; return; }
    assert(command == LCD_RAMWR_CMD && format == LCD_COLMOD_16BIT);
    ++writes;
    if (mode == NO_WRITES || (mode == BAD_ROW && row == 157U)
        || (mode == SECOND_PHASE_STUCK && writes > 640U)) {
        return;
    }
    pixels[row][column] = value;
}
static uint16_t BSP_lcdReadDat_(void) {
    assert(command == LCD_RAMRD_CMD && format == LCD_COLMOD_READ);
    unsigned const cycle = readCycle++;
    assert(cycle < 3U);
    if (cycle == 0U) { return 0xA55AU; } // dummy must be discarded
    if (cycle == 1U) { ++reads; }
    if (mode == STUCK_READ) { return 0xFFFFU; }
    uint16_t const pixel = pixels[row][column];
    unsigned const r = pixel >> 11U;
    unsigned const g = (pixel >> 5U) & 63U;
    unsigned const b = pixel & 31U;
    // RGB666 words, with nonzero don't-care bits and next-pixel data.
    unsigned const r6 = (r << 1U) | (r >> 4U);
    unsigned const b6 = (b << 1U) | (b >> 4U);
    return cycle == 1U ? (uint16_t)((r6 << 10U) | (g << 2U) | 0x0303U)
        : (uint16_t)((b6 << 10U) | 0x03FFU);
}
static uint16_t BSP_lcdReadId_(void) {
    assert(resetCommands == 1U);
    assert(tick - resetAt >= LCD_SWRESET_SETTLE_MS);
    ++idReads;
    if (forceNoId) { return 0xFFFFU; }
    if (commMisses != 0U) { --commMisses; return 0U; }
    return LCD_ID_ST7789;
}
static void BSP_dbgLcdBootId_(void) {
    lcd_dbgBootId_ = BSP_lcdReadId_();
}
static void BSP_lcdPanelInit_(void) {
    assert(sleepOuts != 0U);
    assert(tick - sleepAt >= LCD_SLPOUT_SETTLE_MS);
    ++tables;
    format = LCD_COLMOD_16BIT;
}
static bool BSP_lcdReady_(void) {
    assert(tables != 0U);
    if (forceSleep) { return false; }
    if (readyMisses != 0U) { --readyMisses; return false; }
    return true;
}
static void BSP_lcdFill_(uint16_t colour) {
    assert(lcd_dbgBoot_.verified == 1U);
    assert(format == LCD_COLMOD_16BIT && colour == LCD_BLACK);
    ++fills;
    memset(pixels, 0, sizeof(pixels));
}
static void reset(unsigned fault) {
    memset(pixels, 0, sizeof(pixels));
    memset(&lcd_dbgBoot_, 0, sizeof(lcd_dbgBoot_));
    reads = writes = fills = tables = sleepOuts = readyMisses = 0U;
    format = LCD_COLMOD_16BIT;
    tick = 0U;
    forceSleep = false;
    forceNoId = false;
    commMisses = idReads = resetCommands = 0U;
    resetAt = sleepAt = 0U;
    lcd_dbgBootDispOn_ = false;
    mode = fault;
}
int main(void) {
    reset(GOOD);
    for (uint16_t y = 0U; y < 320U; ++y) {
        unsigned const left = lcdSentinelX_(y, 0U);
        unsigned const right = lcdSentinelX_(y, 1U);
        assert(left < 240U && right < 240U && left != right);
        assert(left + right == 239U);
    }
    assert(lcdSentinelX_(0U, 0U) == 0U);
    assert(lcdSentinelX_(319U, 0U) == 239U);
    assert(BSP_lcdWritable_());
    assert(writes == 1280U && reads == 1280U);
    assert(format == LCD_COLMOD_16BIT);
    for (unsigned fault = NO_WRITES; fault <= STUCK_READ; ++fault) {
        reset(fault);
        assert(!BSP_lcdWritable_());
        assert(format == LCD_COLMOD_16BIT);
    }
    reset(NO_WRITES);
    // A pre-existing correct first image must fail the exchanged image.
    for (uint16_t y = 0U; y < 320U; ++y) {
        for (unsigned d = 0U; d < 2U; ++d) {
            pixels[y][lcdSentinelX_(y, d)] = lcdSentinelColour_(y, d, 0U);
        }
    }
    assert(!BSP_lcdWritable_() && writes == 1280U && reads == 641U);
    reset(GOOD);
    readyMisses = 2U;
    BSP_lcdInit();
    assert(lcd_dbgBoot_.verified == 1U && lcd_dbgBoot_.attempts == 3U);
    assert(lcd_dbgBoot_.readyFailures == 2U && tables == 3U);
    assert(fills == 1U && reads == 1280U); // no readback after black fill
    assert(lcd_dbgBootDispOn_ && sleepOuts == 3U);
    reset(SECOND_PHASE_STUCK);
    BSP_lcdInit();
    assert(!lcd_dbgBoot_.verified && !fills);
    assert(lcd_dbgBoot_.writeFailures == LCD_INIT_ATTEMPTS);
    assert(lcd_dbgBootDispOn_); // existing exhausted-budget behavior
    reset(GOOD);
    forceSleep = true;
    BSP_lcdInit();
    assert(lcd_dbgBoot_.readyFailures == LCD_INIT_ATTEMPTS);
    assert(!writes && !fills && !lcd_dbgBoot_.verified);
    assert(tables == LCD_INIT_ATTEMPTS);
    // Communication retries overlap the pre-SLPOUT recovery interval.
    reset(GOOD);
    commMisses = 2U;
    BSP_lcdInit();
    assert(lcd_dbgBoot_.commFailures == 2U && resetCommands == 1U);
    assert(sleepAt == 125U && tick == 130U);
    assert(lcd_dbgBoot_.verified && fills == 1U);
    reset(GOOD);
    commMisses = 20U;
    BSP_lcdInit();
    assert(sleepAt == 210U && tick == 215U);
    assert(lcd_dbgBoot_.commFailures == 20U && resetCommands == 1U);
    reset(GOOD);
    forceNoId = true;
    BSP_lcdInit();
    assert(idReads == LCD_INIT_ATTEMPTS && resetCommands == 1U);
    assert(lcd_dbgBoot_.commFailures == LCD_INIT_ATTEMPTS);
    assert(!sleepOuts && !tables && !writes && !fills);
    assert(!lcd_dbgBootDispOn_ && !lcd_dbgBoot_.verified);
    assert(lcd_dbgBootId_ == 0xFFFFU);
    reset(GOOD);
    tick = 1000U;
    BSP_lcdInit();
    assert(resetAt == 1000U && sleepAt == 1120U && tick == 1125U);
    tick = 2U;
    BSP_lcdWaitSince_(UINT32_MAX - 1U, 5U);
    assert(tick == 3U); // unsigned wrap in minimum-interval accounting
    puts("PASS: X coverage, colour decode, fault rejection,");
    puts("      format restoration, timing guards, retries and exhaustion");
}
