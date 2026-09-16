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
// The bus setup, the vendor register sequence and the panel geometry live here.
// The drawing API (window + pixels) arrives with the display layer. Bring-up
// reports twice on the trace channel: DISPON and one RDID read, which is the
// cheap "panel is talking" line on the bench.

//============================================================================
// One-time bus setup (GPIO + FSMC) and backlight on. Pure register writes,
// no blocking, no interrupt dependency, so it is safe to call from an AO
// init transition.
void BSP_lcdInit(void);

// One step of the panel bring-up, meant to be called from the AO once per
// timeout signal: SLPOUT, then the register table one beat later (which covers
// the 120 ms the panel asks for between the two), then one RDID read. Every
// later call returns immediately.
void BSP_lcdBringUp(void);

#endif // BSP_LCD_H_
