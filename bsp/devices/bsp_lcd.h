//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef BSP_LCD_H_
#define BSP_LCD_H_

#include <stdbool.h>
#include <stdint.h>

//============================================================================
// ATK-MD0240 (240x320, ST7789) on FSMC Bank1 NOR/SRAM4:
//   CS  = FSMC_NE4 (PG12)  -> bank base 0x6C000000
//   RS  = FSMC_A10 (PG0)   -> command at base, data at base + 0x800
//   WR  = FSMC_NWE (PD5)
//   RD  = FSMC_NOE (PD4)
//   D0..D15 = FSMC_D0..D15
//   BL  = PB0 (active high)
//   RST is tied to the board RESET net, so there is no reset GPIO.
//
// The bus setup, the vendor register sequence and the panel geometry live
// here. The drawing API (window + pixels) arrives with the display layer.

//============================================================================
// One-time panel bring-up: bus setup (GPIO + FSMC), the panel's own waits
// around SLPOUT and the vendor register sequence, then the frame memory
// cleared to black and display on, and the backlight last. Bring-up ends with
// the glass showing a frame memory that is entirely black - every retry and
// every unstable state above happens behind a dark glass, and the light comes
// on only once the panel is in a defined state. It blocks for the better
// part of 250 ms, so it
// belongs to BSP_init() - before the scheduler exists, never inside a task -
// and it is the only writer on this bus until the display layer takes over.
// On the trace channel it reports DISPON plus one RDID read, the cheap
// "panel is talking" line on the bench.
void BSP_lcdInit(void);

// Read a few frame memory pixels back and report them: four points down the
// left edge, spanning both halves of the panel. Called by the AO some time
// after bring-up, so the records land on a terminal that is already attached.
void BSP_lcdCheck(void);

// The bring-up facts (display on, identification read) again, for the first
// task to report: bring-up itself cannot push records (see bsp.c).
void BSP_lcdBootReport(void);

// Drawing API for the display layer: open a window, then push pixels into it.
// The window writes go straight to the FSMC bus, so they belong to whoever
// owns the panel at run time (the UI thread); there is no locking here.
//
// One 8080 16-bit transfer carries one RGB565 pixel, so the halfword goes out
// as it is - no byte swapping on this path.
void BSP_lcdSetWindow(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye);

// The pixel push runs on DMA1 channel 6: the caller arms the channel and
// returns, and the transfer's completion runs the hook below from the
// channel's interrupt.
//
// Both ends are memory mapped - the strip in SRAM, the panel window on
// FSMC bank 4 - so this is a memory to memory transfer and no request line is
// involved; the FSMC's own wait states pace the writes at the panel's timing.
// That is what makes M2M legal here, and it is exactly why the flash read
// cannot use it: the SPI data register is a handshake endpoint (charter §3).
//
// One shot, one strip at a time. The panel has one owner at run time (the
// UI thread) and it is blocked while the transfer runs, so nothing else is on
// this bus.
//
// count is in pixels; the channel moves two of them per access when the count
// is even and the source is word aligned, one per access otherwise. The
// ceiling is the 16-bit transfer counter, an assertion here and not a plan:
// LVGL's partial buffer is 240x32 = 7680. The source has to be at least
// halfword aligned, which the strip buffer is.
void BSP_lcdWritePixelsDma(uint16_t const *pixels, uint32_t count);

// Completion hook, run in the DMA interrupt. The display adapter wakes its
// thread from here and calls lv_display_flush_ready(); keep it to that. No
// blocking, no waiting, nothing that takes a lock.
void BSP_lcdDmaSetDoneHook(void (*hook)(void));

// True when the last transfer ended in a DMA error. The adapter reads it
// after its wait, so the assertion and its record happen on the thread
// instead of inside an interrupt.
bool BSP_lcdDmaFailed(void);

// Is the channel still moving pixels? The arm in BSP_lcdWritePixelsDma is a
// register level one that enables no interrupts, so the caller waits by
// polling this instead of by parking on the transfer's completion. See the
// display adapter for why the wait is a poll and not a block (charter §11).
bool BSP_lcdDmaBusy(void);

// DMA1_Channel6_IRQHandler is defined in bsp_lcd.c, next to the channel it
// serves. It is not in bsp/platform/stm32f1xx_it.c because that file is
// shared with the bootloader, which links no panel driver.

#endif // BSP_LCD_H_
