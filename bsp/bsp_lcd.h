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
// What the probe covers, once per AO beat:
//   RDID (0x04):        CS, RS, WR, RD and D0..D7, since the ID comes back in
//                       the low byte. It also confirms the IM[3:0] = 001
//                       strap that selects the 8080 16 bit interface.
//   GRAM (0x2C/0x2E):   all 16 data lines, both directions, by writing four
//                       patterns and reading them back. On a 16 bit bus the
//                       RGB565 bits land as D15..D11 = R, D10..D5 = G and
//                       D4..D0 = B, so the patterns exercise every line at
//                       both rails and in alternating pairs.
//   DWT CYCCNT:         plain liveness of the cycle counter behind the
//                       ledOn/ledOff timestamps.
//
// Bring-up and the visible signal:
//   Beat 1 sends SLPOUT, beat 2 sends the rest of the vendor register
//   sequence (MADCTL, colour format, porch, power, gamma, inversion, DISPON)
//   so the 120 ms the panel asks for costs nothing. From beat 3 on, each beat
//   paints a 16 row band of the frame with a colour that cycles when the
//   frame wraps, which is both a continuous write-path load and something to
//   look at on the bench.

//============================================================================
// One-time bus setup (GPIO + FSMC) and backlight on. Pure register writes,
// no blocking, no interrupt dependency, so it is safe to call from an AO
// init transition.
void BSP_lcdInit(void);

// One probe beat, meant to be called from the AO on every timeout signal.
// Non-blocking by construction: the panel wake-up spends one register write
// per beat (SLPOUT, then COLMOD one beat later) and every check is a plain
// bus transaction. Results go out on the trace channel.
void BSP_lcdProbe(void);

#endif // BSP_LCD_H_
