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
// One-time panel bring-up: bus setup (GPIO + FSMC), backlight on, then the
// vendor register sequence with the panel's own waits around it. It blocks
// for the better part of 250 ms, so it belongs to BSP_init() - before the
// scheduler exists, never inside a task - and it is the only writer on this
// bus until the display layer takes over. Returns with the panel out of
// sleep and its registers loaded; on the trace channel it reports DISPON
// plus one RDID read, the cheap "panel is talking" line on the bench.
void BSP_lcdInit(void);

#endif // BSP_LCD_H_
