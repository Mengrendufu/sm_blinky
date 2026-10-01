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

// Drawing API for the display layer: open a window, then push pixels into it.
// Both write straight to the FSMC bus, so they belong to whoever owns the
// panel at run time (the UI thread); there is no locking here.
//
// One 8080 16-bit transfer carries one RGB565 pixel, so the halfword goes out
// as it is - no byte swapping on this path.
void BSP_lcdSetWindow(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye);
void BSP_lcdWritePixels(uint16_t const *pixels, uint32_t count);

#endif // BSP_LCD_H_
