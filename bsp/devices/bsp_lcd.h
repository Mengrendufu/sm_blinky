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

// ATK-MD0240 (240x320, ST7789), FSMC Bank1 NOR/SRAM4.
// CS=PG12/NE4, RS=PG0/A10, WR=PD5/NWE, RD=PD4/NOE, BL=PB0.
// Data is RGB565 over D0..D15. RESET is tied to the board RESET net.

//============================================================================
// Startup only: GPIO/FSMC/DMA setup, panel sleep-out and readiness retries.
// Runs before the scheduler using blocking HAL delays and GRAM readback.
// Clears GRAM and enables display output; the backlight remains off.
void BSP_lcdInit(void);

// Reports cached DISPON/RDID bring-up facts once the tracer is available.
// Does not read the live panel or perform a new coverage check.
void BSP_dbgLcdBootReport(void);

//============================================================================
// Waiting capabilities implemented by the display owner, not by the BSP.
// context is opaque and outlives every transfer. isCaller accepts only the
// owner, never an ISR. waitDone consumes completion, including an early
// notification; false means timeout. notifyDone runs in the DMA ISR and
// must not block. All time arguments are milliseconds.
typedef struct {
    void *context;
    bool (*isCaller)(void *context);
    bool (*waitDone)(void *context, uint32_t timeoutMs);
    void (*notifyDone)(void *context);
} BSP_lcdWaitOps;

// Bind once from the owner before run-time writes or panel control.
// Copies the structure. No queue or mutex is provided.
void BSP_lcdBindWait(BSP_lcdWaitOps const *ops);

// Inclusive coordinates, contiguous RGB565 pixels, at least halfword aligned.
// Sets the window, starts DMA and waits synchronously before returning the
// borrowed buffer. Count is derived from the rectangle. One DMA transfer:
// at most 65,535 units, each carrying 2 pixels for aligned even counts,
// otherwise 1 pixel. False reports DMA error; timeout is fail-stop because
// DMA may still own the buffer. Waiting behavior comes from the bound ops.
bool BSP_lcdWriteAreaWait(uint16_t xs, uint16_t ys, uint16_t xe,
                          uint16_t ye, uint16_t const *pixels);

// Run-time owner only. UI turns on backlight after its first complete frame.
void BSP_lcdBacklight(bool on);

// Re-send inversion-on (0x21), the board's polarity keep-alive. Scheduling
// this operation belongs to the display owner; it must not overlap DMA.
void BSP_lcdReassert(void);

#endif // BSP_LCD_H_
