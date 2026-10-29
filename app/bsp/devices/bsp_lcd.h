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
// LCD object -- one physical FSMC panel, owned by BSP. Obtain it after init;
// do not copy the object or change its members directly.
typedef struct BSP_Lcd BSP_Lcd;
typedef void (*BSP_lcdDone)(void *context, bool ok);

// Pixel transport under an already-open window. At most 65,535 halfwords,
// or 65,535 words for aligned even pixel counts. Exactly one done callback:
// Poll completes inline; DMA completes in ISR or inline on start failure.
// Bind these tables to the object; use only the public methods below.
typedef struct {
    void (*writePixels)(void *device, uint16_t const *pixels, uint32_t count,
                        BSP_lcdDone done, void *context);
} BSP_lcdOperations;

extern BSP_lcdOperations const BSP_lcdPollOperations;
extern BSP_lcdOperations const BSP_lcdDmaOperations;

// Waiting operations owned by the display adapter. isCaller accepts only
// the owner, never an ISR. waitDone consumes one completion, including an
// early notifyDone. notifyDone supports inline and ISR calls. yield is a
// scheduling point between bounded pixel transfers; it may be a no-op.
// Tables and context must outlive their binding. Times are milliseconds.
typedef struct {
    bool (*isCaller)(void *context);
    bool (*waitDone)(void *context, uint32_t timeoutMs);
    void (*notifyDone)(void *context);
    void (*yield)(void *context);
} BSP_lcdWaitOperations;

struct BSP_Lcd {
    void *device;
    BSP_lcdOperations const *operations;
    BSP_lcdWaitOperations const *waitOperations;
    void *waitContext;
    // Private state: busy includes window setup, all chunks and waits.
    bool busy;
    volatile bool complete;
    volatile bool ok;
};

BSP_Lcd *BSP_lcdDevice(void);

// Bind before use. The outgoing owner may rebind only when no operation,
// DMA or unconsumed completion remains. No queue or mutex is provided.
void BSP_lcdBind(BSP_Lcd *me, BSP_lcdOperations const *operations,
                 BSP_lcdWaitOperations const *waitOperations,
                 void *waitContext);

//============================================================================
// Synchronous area write: inclusive coordinates and contiguous RGB565 data,
// at least halfword aligned. One window covers all bounded pixel transfers.
// The buffer is borrowed until return, when every transfer has completed.
// False reports a transfer error; lost completion is fail-stop because DMA
// may still own the buffer. DMA needs live interrupts; Poll does not.
bool BSP_lcdWriteArea(BSP_Lcd *me, uint16_t xs, uint16_t ys, uint16_t xe,
                      uint16_t ye, uint16_t const *pixels);

// Same owner/idle contract as area writes. The HSM enables backlight on
// Splash entry, before rendering. Polarity timing belongs to the adapter.
void BSP_lcdBacklight(BSP_Lcd *me, bool on);
void BSP_lcdReassert(BSP_Lcd *me);

#endif // BSP_LCD_H_
