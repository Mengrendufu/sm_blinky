//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef BSP_FLASH_H_
#define BSP_FLASH_H_

#include <stdbool.h>
#include <stdint.h>

//============================================================================
// W25Q128 (16 MB SPI NOR) on SPI2, from the core board IO allocation table
// (rows 73-76):
//   CS   = PB12 (F_CS)
//   SCK  = PB13
//   MISO = PB14
//   MOSI = PB15
// SPI1 is not an option here: on this board it belongs to the VS1053 socket.
// The wireless socket shares SPI2 and gives up its own chip select.
//
// Board bring-up initializes the bus. Lifecycle owners bind the object
// before access: asset loading, AO initialization/run time, or bootloader.

// Device initialization shared by all phases.
void BSP_flashInit(void);

//============================================================================
// Flash object -- this board has one physical SPI2 device, owned by the BSP.
// Obtain it after BSP_flashInit; never copy it or alter its members directly.
typedef struct BSP_Flash BSP_Flash;
typedef void (*BSP_flashDone)(void *context, bool ok);

//============================================================================
// Transport operations supplied by BSP; select one table at binding time.
// Bind tables to the object; call only the unified public methods below.
// Each method completes one bounded transaction, releases CS and calls done
// exactly once (also on failure). Poll completes inline; DMA may complete
// inline on start failure or from an ISR. Buffers remain borrowed until done.
// program never crosses a page; erase only sends one sector command.
// Completion here does NOT mean the chip's program/erase BUSY has cleared.
typedef struct {
    void (*read)(void *device, uint32_t address, void *dst, uint32_t len,
                 BSP_flashDone done, void *context);
    void (*program)(void *device, uint32_t address, void const *src,
                    uint32_t len, BSP_flashDone done, void *context);
    void (*erase)(void *device, uint32_t address,
                  BSP_flashDone done, void *context);
} BSP_flashOperations;

extern BSP_flashOperations const BSP_flashPollOperations;
extern BSP_flashOperations const BSP_flashDmaOperations;

//============================================================================
// Waiting operations required by BSP, supplied and owned by the application.
// waitDone consumes one completion, even if notifyDone ran before the wait.
// notifyDone supports both inline and ISR calls. sleepMs is a busy delay or
// a yielding delay; yield is a scheduling point between bounded transfers.
// now/elapsedMs preserve the caller's existing time source and wrap rules.
// No callback/table/context may expire while bound to the device.
typedef struct {
    bool (*isCaller)(void *context);
    bool (*waitDone)(void *context, uint32_t timeoutMs);
    void (*notifyDone)(void *context);
    void (*sleepMs)(void *context, uint32_t delayMs);
    void (*yield)(void *context);
    uint32_t (*now)(void *context);
    uint32_t (*elapsedMs)(void *context, uint32_t started);
} BSP_flashWaitOperations;

struct BSP_Flash {
    void *device;
    BSP_flashOperations const *operations;
    BSP_flashWaitOperations const *waitOperations;
    void *waitContext;
    // Private execution state: busy covers the entire public operation,
    // including all pages/chunks, completion consumption and chip BUSY.
    bool busy;
    volatile bool complete;
    volatile bool ok;
};

BSP_Flash *BSP_flashDevice(void);

// Lifecycle owner binds before use and may rebind only between operations,
// with no DMA, chip BUSY or unconsumed completion. Binding provides no lock.
// The outgoing owner's isCaller must accept the binder. The incoming owner
// is checked at operation entry, so it may be a task not yet scheduled.
void BSP_flashBind(BSP_Flash *me, BSP_flashOperations const *operations,
                   BSP_flashWaitOperations const *waitOperations,
                   void *waitContext);

//============================================================================
// Unified synchronous API -- all phases use the same object and methods.
// Returning succeeds only after data transfer AND chip program/erase finish.
// HAL tick must advance in both modes. DMA also needs completion IRQs.
// Writes split at page boundaries; destination must already be erased.
// Erase addresses are aligned to 4 KB. False reports transfer/device failure.
// Lost completion is fail-stop: an active DMA must not outlive its buffer.
bool BSP_flashRead(BSP_Flash *me, uint32_t address, void *dst, uint32_t len);
bool BSP_flashWrite(BSP_Flash *me, uint32_t address,
                    void const *src, uint32_t len);
bool BSP_flashEraseSector(BSP_Flash *me, uint32_t address);

#endif // BSP_FLASH_H_
