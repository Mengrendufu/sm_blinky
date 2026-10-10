//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef BSP_SRAM_H_
#define BSP_SRAM_H_

//============================================================================
// IS62WV51216 (512K x 16, 1 MB) on FSMC Bank1 NOR/SRAM3:
//   CS    = FSMC_NE3 (PG10)          -> bank base 0x68000000
//   WE    = FSMC_NWE (PD5)           } shared with the LCD
//   OE    = FSMC_NOE (PD4)           }
//   D0..D15 = FSMC_D0..D15           } shared with the LCD
//   NBL0/NBL1 = PE0/PE1              -> byte lanes, SRAM only
//   A0..A18 = PF0..PF5, PF12..PF15, PG0..PG5, PD11..PD13 (SRAM only, except
//             A10, which the LCD uses as RS)
//   Pin map: ALIENTEK core board IO allocation table, Sheet1.
//
// The board does not wire the SRAM address pins in order (see the schematic);
// that mapping is a bijection, so software does not need to know it.

//============================================================================
// One-time bus setup (GPIO + FSMC bank 3). Pure register writes, no blocking,
// so it is safe to call from an AO init transition.
void BSP_sramInit(void);


#endif // BSP_SRAM_H_
