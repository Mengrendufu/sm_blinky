//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "stm32f1xx_hal.h"
#include "bsp_flash.h"
#include "nor_w25q128.h"
#include "sm_assert.h"

//============================================================================
#define FLASH_CS_PORT   GPIOB
#define FLASH_CS_PIN    GPIO_PIN_12

#define FLASH_STATUS_CMD    0x05U
#define FLASH_READ_CMD      0x03U
#define FLASH_WREN_CMD      0x06U
#define FLASH_PROGRAM_CMD   0x02U
#define FLASH_SECTOR_CMD    0x20U
#define FLASH_XFER_MS       100U

// One HAL call carries this many bytes. The size argument is a uint16_t, so
// 65535 is the ceiling; half of it keeps a chunk at 7.3 ms on the wire at
// 18 MHz, well inside FLASH_XFER_MS.
//
// Why blocks at all: the byte at a time loop this replaced spent one HAL call
// per byte, and that call overhead - not the wire - was the limit. Measured
// on the boot's 460,800 byte asset read, same instrument, both directions:
// byte at a time 458 ms (1.0 MB/s), blocks 196 ms (2.35 MB/s) against the
// 2.25 MB/s the bus carries at 18 MHz, so blocks land on the wire limit.
#define FLASH_BLOCK_MAX     32768UL

// Operating clock: PCLK1 / 2 = 18 MHz, the fastest the peripheral offers. A
// sweep of all four dividers (2.25, 4.5, 9, 18 MHz) came back clean at every
// step - sixteen reads of the same address per clock, all identical and all
// matching each other - so there is nothing to be gained by running slower.
#define FLASH_SPI_PRESCALER SPI_BAUDRATEPRESCALER_2

// Trace records: ==FLASH==[JEDEC][<mfr> <type> <cap>]==[W25Q128/??????]== and
// ==FLASH==[ARRAY][<six bytes at address 0>]==. The second is the "this is
// memory and it reads" line; the first one is identity only.


// One DMA transaction is at most 32 KB. Chunking stays inside this driver.
#define FLASH_DMA_CHUNK 32768UL

typedef void (*BSP_flashDoneCb)(bool ok);

static SPI_HandleTypeDef flash_spi_;

//============================================================================
static void BSP_flashGpioInit_(void);
static void BSP_flashSpiInit_(void);
// The two block calls keep their last HAL status: a transfer that failed used
// to look exactly like one that worked (the destination simply kept its old
// contents), a trap this file walked into once already.
static uint8_t flashTxStatus_ = 0xFFU;
static uint8_t flashRxStatus_ = 0xFFU;

static uint8_t BSP_flashXfer_(uint8_t out);
static void BSP_flashAddr_(uint8_t out[3], uint32_t address);
static bool BSP_flashWriteBlock_(uint8_t const *tx, uint32_t len);
static bool BSP_flashReadBlock_(uint8_t *rx, uint32_t len);
static bool BSP_flashExchange_(uint8_t cmd, uint8_t const *tx, uint32_t txLen,
                               uint8_t *rx, uint32_t rxLen);
static uint8_t BSP_flashReadStatus_(void);
static bool BSP_flashWriteEnable_(void);


//============================================================================
// DMA primitives shared by boot loading and the bound wait API.
// Wait capabilities choose flag polling or task notifications. Every DMA
// path needs interrupts enabled.
//
// Shape: one call, one transaction - CS low, command and address from the
// CPU, the data moved by the channel, CS raised and the caller called back
// from the completion interrupt. A read longer than FLASH_DMA_CHUNK is the
// caller's loop (each chunk is its own transaction, command and address
// included; that costs a few microseconds against 14 ms of transfer).
//
// HAL_SPI_Receive_DMA() in master, two line mode is
// HAL_SPI_TransmitReceive_DMA(hspi, pData, pData, Size) - the same in place
// trick as the blocking call - so the transmit channel reads the very buffer
// the receive channel fills. That is harmless here (the device ignores DI
// while it shifts data out) and it keeps this path at zero extra RAM.
// One arm moves one chunk and no more: the F1 HAL does not support starting
// the next transfer from inside its own completion callback (its
// SPI_DMAReceiveCplt keeps working with the handles after the callback
// returns, so re-arming there writes through a stale pointer - found the hard
// way, the fault stash named that function). Chunking is the caller's job,
// which is also where the yield between chunks belongs.
// 5 is configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY: the completion callback
// wakes tasks, so a numerically lower priority would be illegal to call from.
#define FLASH_DMA_IRQ_PRIO  5U

// How long one chunk may take before the boot path gives up on it. A 32 KB
// chunk is 14.4 ms on the wire, so this is generous by an order of magnitude.
#define FLASH_DMA_BOOT_MS 200U

static volatile bool flashDmaFlag_;

static void BSP_flashDmaFlagCb_(bool ok) {
    (void)ok; // flashDmaFailed_ carries the answer to the waiter

    flashDmaFlag_ = true;
}
//............................................................................
static void BSP_flashDmaInit_(void);
static void BSP_flashDmaChunk_(void);
static void BSP_flashDmaFinish_(bool ok);

static DMA_HandleTypeDef flashDmaRx_;   // DMA1_Channel4, SPI2_RX
static DMA_HandleTypeDef flashDmaTx_;   // DMA1_Channel5, SPI2_TX
static volatile bool flashDmaBusy_;
static volatile bool flashDmaFailed_;
static uint8_t *flashDmaDst_;
static uint32_t flashDmaLeft_;
static BSP_flashDoneCb flashDmaDone_;

//============================================================================
void BSP_flashInit(void) {
    BSP_flashGpioInit_();
    BSP_flashSpiInit_();
    BSP_flashDmaInit_();

    // Throwaway read. The first transaction after power-up came back wrong on
    // every boot we measured (three boots, three answers) while every later
    // one matched, so the first one is spent here instead of on data.
    (void)BSP_flashReadStatus_();
}
//............................................................................
// Read Data (03h) over an arbitrary length. The device increments the
// address by itself for as long as the clock keeps coming, so one
// transaction covers the whole block. No overrun to clean up either: the
// byte-wise exchange reads the receive register on every unit, so RXNE
// never stays set.
bool BSP_flashRead(uint32_t address, uint8_t *dst, uint32_t len) {
    uint8_t addr[3];

    BSP_flashAddr_(addr, address);

    if ((dst == (uint8_t *)0) || (len == 0U) || (address >= FLASH_DEV_SIZE)
        || (len > (FLASH_DEV_SIZE - address))) {
        return false;
    }

    // The device must not be busy here. At run time the flash thread is the
    // only bus user and every program or erase waits BUSY out before it
    // returns, so this call never races one.
    return BSP_flashExchange_(FLASH_READ_CMD, addr, 3U, dst, len);
}

//............................................................................
static void BSP_flashDmaInit_(void) {
    __HAL_RCC_DMA1_CLK_ENABLE();

    flashDmaRx_.Instance = DMA1_Channel4;
    flashDmaRx_.Init.Direction = DMA_PERIPH_TO_MEMORY;
    flashDmaRx_.Init.PeriphInc = DMA_PINC_DISABLE;
    flashDmaRx_.Init.MemInc = DMA_MINC_ENABLE;
    flashDmaRx_.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    flashDmaRx_.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    flashDmaRx_.Init.Mode = DMA_NORMAL;
    flashDmaRx_.Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&flashDmaRx_) != HAL_OK) {
        SM_onAssert("flash", __LINE__);
    }

    flashDmaTx_.Instance = DMA1_Channel5;
    flashDmaTx_.Init.Direction = DMA_MEMORY_TO_PERIPH;
    flashDmaTx_.Init.PeriphInc = DMA_PINC_DISABLE;
    flashDmaTx_.Init.MemInc = DMA_MINC_ENABLE;
    flashDmaTx_.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    flashDmaTx_.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    flashDmaTx_.Init.Mode = DMA_NORMAL;
    flashDmaTx_.Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&flashDmaTx_) != HAL_OK) {
        SM_onAssert("flash", __LINE__);
    }

    // The SPI takes both: the receive channel carries the data, the transmit
    // one only keeps the clock running while it does. __HAL_LINKDMA(), not a
    // plain assignment: the HAL's completion callbacks look the SPI handle up
    // through hdma->Parent, and the macro is the only thing that sets it.
    // Without it Parent stays NULL, the callback reads ErrorCode through it
    // and the board faults into erased flash (the fault stash named it).
    __HAL_LINKDMA(&flash_spi_, hdmarx, flashDmaRx_);
    __HAL_LINKDMA(&flash_spi_, hdmatx, flashDmaTx_);

    HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, FLASH_DMA_IRQ_PRIO, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, FLASH_DMA_IRQ_PRIO, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
}
//............................................................................
// One chunk, armed and left alone. What happens next happens in the
// completion interrupt.
static void BSP_flashDmaChunk_(void) {
    if (HAL_SPI_Receive_DMA(&flash_spi_, flashDmaDst_,
                            (uint16_t)flashDmaLeft_) != HAL_OK) {
        BSP_flashDmaFinish_(false);
    }
}
//............................................................................
// The end of the read, from interrupt context: CS high, the state cleared,
// then the caller's callback. The callback is taken and cleared first, so a
// callback that starts another read cannot have it overwritten.
static void BSP_flashDmaFinish_(bool ok) {
    BSP_flashDoneCb const cb = flashDmaDone_;

    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);

    flashDmaDone_ = (BSP_flashDoneCb)0;
    flashDmaFailed_ = !ok;
    flashDmaBusy_ = false;

    if (cb != (BSP_flashDoneCb)0) {
        cb(ok);
    }
}
//............................................................................
static void BSP_flashReadDma_(uint32_t address, uint8_t *dst, uint32_t len,
                      BSP_flashDoneCb done) {
    uint8_t addr[3];

    BSP_flashAddr_(addr, address);

    if ((dst == (uint8_t *)0) || (len == 0U) || (len > FLASH_DMA_CHUNK)
        || flashDmaBusy_ || (address >= FLASH_DEV_SIZE)
        || (len > (FLASH_DEV_SIZE - address))) {
        // A refusal is reported the way a failed transfer is, so a caller
        // that waits on the callback always reads the truth out of
        // flashDmaFailed_.
        flashDmaFailed_ = true;
        if (done != (BSP_flashDoneCb)0) {
            done(false);
        }
        return;
    }

    flashDmaDst_ = dst;
    flashDmaLeft_ = len;
    flashDmaDone_ = done;
    flashDmaFailed_ = false;
    flashDmaBusy_ = true;

    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET);
    (void)BSP_flashXfer_(FLASH_READ_CMD);
    (void)BSP_flashWriteBlock_(addr, 3U);

    BSP_flashDmaChunk_();
}
//............................................................................
// One page, moved by DMA: the write side of BSP_flashReadDma above, for a run
// time writer. One arm is one transaction (write enable, then command and
// address from the CPU, the payload from the channel, chip select raised in
// the completion interrupt).
//
// The device is still programming when that interrupt fires - tPP is 0.7 to
// 3 ms - so the caller waits afterwards with BSP_flashIsReady_(). That is
// deliberate: polling the device from the interrupt would hold the CPU for
// milliseconds inside an ISR, and the caller is where the yield belongs.
static void BSP_flashProgramPageDma_(uint32_t address, uint8_t const *data,
                             uint32_t len, BSP_flashDoneCb done) {
    uint8_t addr[3];

    BSP_flashAddr_(addr, address);

    // The device wraps inside a page and would silently program its start
    // again, so one DMA transaction must never cross the page boundary.
    if ((data == (uint8_t const *)0) || (len == 0U)
        || (len > FLASH_DEV_PAGE)
        || ((address % FLASH_DEV_PAGE) + len > FLASH_DEV_PAGE)
        || flashDmaBusy_ || (address >= FLASH_DEV_SIZE)
        || (len > (FLASH_DEV_SIZE - address))) {
        flashDmaFailed_ = true;
        if (done != (BSP_flashDoneCb)0) {
            done(false);
        }
        return;
    }

    // Its own chip select cycle, so it goes before the one below is pulled
    // low: the program command is ignored without a set write enable latch.
    if (!BSP_flashWriteEnable_()) {
        flashDmaFailed_ = true;
        if (done != (BSP_flashDoneCb)0) {
            done(false);
        }
        return;
    }

    flashDmaDone_ = done;
    flashDmaFailed_ = false;
    flashDmaBusy_ = true;

    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET);
    (void)BSP_flashXfer_(FLASH_PROGRAM_CMD);
    (void)BSP_flashWriteBlock_(addr, 3U);

    if (HAL_SPI_Transmit_DMA(&flash_spi_, (uint8_t *)(uintptr_t)data,
                             (uint16_t)len) != HAL_OK) {
        BSP_flashDmaFinish_(false);
    }
}
//............................................................................
// The run time path waits on a task notification. Before the scheduler there
// is no task to notify, so this one polls a flag the completion interrupt
// sets - the same interrupt, a different way of waiting. It is here for
// Asset_load, which used to crawl at 0.43 MB/s through HAL's polled block
// path and now runs at the wire limit. Safe before the scheduler because the
// callback it installs does nothing but set that flag: no FreeRTOS call, no
// task, nothing that needs a kernel.
bool BSP_flashReadDmaWait(uint32_t address, uint8_t *dst, uint32_t len) {
    bool ok = true;

    while (ok && (len > 0U)) {
        uint32_t const n = (len > FLASH_DMA_CHUNK) ? FLASH_DMA_CHUNK : len;
        uint32_t const deadline = HAL_GetTick() + FLASH_DMA_BOOT_MS;

        flashDmaFlag_ = false;
        BSP_flashReadDma_(address, dst, n, &BSP_flashDmaFlagCb_);

        while (!flashDmaFlag_) {
            if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
                ok = false;
                break;
            }
        }

        ok = ok && !flashDmaFailed_;
        address += n;
        dst += n;
        len -= n;
    }

    return ok;
}
//............................................................................
// One poll of the status register: the caller owns how long to wait and
// whether to yield while it does. The boot path polls without a scheduler;
// the bound wait API below sleeps between polls.
static bool BSP_flashIsReady_(void) {
    return ((BSP_flashReadStatus_() & 0x01U) == 0U);
}
//............................................................................
// HAL's callbacks, called from the channel interrupts. The name is the one
// the HAL actually calls: HAL_SPI_Receive_DMA sets the handle to BUSY_RX
// before it hands over to the transmit-receive routine, so the completion
// goes to SPI_DMAReceiveCplt, which ends in HAL_SPI_RxCpltCallback. Both
// channels report errors through the error callback.
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi != &flash_spi_) {
        return;
    }

    BSP_flashDmaFinish_(true);   // one transaction per arm: never re-arm here
}
//............................................................................
// The transmit direction ends here: HAL_SPI_Transmit_DMA puts
// SPI_DMATransmitCplt on the transmit channel unconditionally, without the
// state trick the receive path has, and that routine waits for TXE and BSY
// before it calls in - which is what makes raising chip select at this point
// correct. It also clears the overrun flag in two line mode because the
// received bytes are never read, so a write needs no dummy receive buffer.
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi != &flash_spi_) {
        return;
    }

    BSP_flashDmaFinish_(true);
}
//............................................................................
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi) {
    if (hspi == &flash_spi_) {
        BSP_flashDmaFinish_(false);
    }
}
//............................................................................
// The two channel vectors, next to the channels they serve. Enabling the NVIC
// line without them lands the interrupt in the startup file's default
// handler, which is an infinite loop - the board goes quiet in the middle of
// the read (found the hard way: the halt showed IPSR 15 with the task
// registers still on the stack and the DMA flags set).
void DMA1_Channel4_IRQHandler(void) {
    HAL_DMA_IRQHandler(&flashDmaRx_);
}
//............................................................................
void DMA1_Channel5_IRQHandler(void) {
    HAL_DMA_IRQHandler(&flashDmaTx_);
}
//............................................................................
static void BSP_flashGpioInit_(void) {
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    // Chip select: plain output, left deselected while the bus comes up.
    gpio.Pin   = FLASH_CS_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(FLASH_CS_PORT, &gpio);
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);

    // SCK and MOSI are driven by the peripheral. MISO is an input in master
    // mode (GPIO_MODE_AF_INPUT is floating input here); the pull-up stays on
    // because that net is shared with the wireless socket and the board puts
    // no pull-up of its own on it.
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pin  = GPIO_PIN_13 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Mode = GPIO_MODE_AF_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin  = GPIO_PIN_14;
    HAL_GPIO_Init(GPIOB, &gpio);
}
//............................................................................
static void BSP_flashSpiInit_(void) {
    flash_spi_.Instance = SPI2;
    flash_spi_.Init.Mode = SPI_MODE_MASTER;
    flash_spi_.Init.Direction = SPI_DIRECTION_2LINES;
    flash_spi_.Init.DataSize = SPI_DATASIZE_8BIT;
    // Mode 0: the flash samples on the first clock edge.
    flash_spi_.Init.CLKPolarity = SPI_POLARITY_LOW;
    flash_spi_.Init.CLKPhase = SPI_PHASE_1EDGE;
    flash_spi_.Init.NSS = SPI_NSS_SOFT;
    flash_spi_.Init.BaudRatePrescaler = FLASH_SPI_PRESCALER;
    flash_spi_.Init.FirstBit = SPI_FIRSTBIT_MSB;
    flash_spi_.Init.TIMode = SPI_TIMODE_DISABLE;
    flash_spi_.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    flash_spi_.Init.CRCPolynomial = 10U; // unused, only kept non-zero
    (void)HAL_SPI_Init(&flash_spi_);
}
//............................................................................
static uint8_t BSP_flashXfer_(uint8_t out) {
    uint8_t in = 0U;

    // SPIN: HAL polls the flags itself, with FLASH_XFER_MS as the deadline,
    // so the CPU stays here for the whole byte. Every read and write of the
    // array goes through this call; the DMA path turns it into a WAIT.
    (void)HAL_SPI_TransmitReceive(&flash_spi_, &out, &in, 1U, FLASH_XFER_MS);
    return in;
}
//............................................................................
// Three address bytes, most significant first. Every command takes the same
// 24 bit address, and the byte order is not something to get wrong in six
// different places.
static void BSP_flashAddr_(uint8_t out[3], uint32_t address) {
    out[0] = (uint8_t)(address >> 16);
    out[1] = (uint8_t)(address >> 8);
    out[2] = (uint8_t)address;
}
//............................................................................
//............................................................................
// One command byte and however many bytes come back, with the chip select low
// around the whole exchange: the datasheet wants it low for command and data
// alike.
//............................................................................
// One transaction, one CS assertion. The command and the address stay byte at
// a time (they are one and three bytes), the data goes out in blocks.
static bool BSP_flashWriteBlock_(uint8_t const *tx, uint32_t len) {
    while (len > 0U) {
        uint16_t const n = (uint16_t)((len > FLASH_BLOCK_MAX)
                                      ? FLASH_BLOCK_MAX : len);

        flashTxStatus_ = (uint8_t)HAL_SPI_Transmit(
            &flash_spi_, (uint8_t *)(uintptr_t)tx, n, FLASH_XFER_MS);
        if (flashTxStatus_ != (uint8_t)HAL_OK) {
            return false;
        }

        tx += n;
        len -= n;
    }

    return true;
}
//............................................................................
// HAL_SPI_Receive() in master, two line mode is a HAL_SPI_TransmitReceive()
// with the same buffer coming and going (see its first branch), so a read
// needs no 0xFF dummy buffer: the device ignores DI while it shifts data out.
static bool BSP_flashReadBlock_(uint8_t *rx, uint32_t len) {
    while (len > 0U) {
        uint16_t const n = (uint16_t)((len > FLASH_BLOCK_MAX)
                                      ? FLASH_BLOCK_MAX : len);

        flashRxStatus_ = (uint8_t)HAL_SPI_Receive(&flash_spi_, rx, n,
                                                  FLASH_XFER_MS);
        if (flashRxStatus_ != (uint8_t)HAL_OK) {
            return false;
        }

        rx += n;
        len -= n;
    }

    return true;
}
//............................................................................
static bool BSP_flashExchange_(uint8_t cmd, uint8_t const *tx,
                               uint32_t txLen, uint8_t *rx,
                               uint32_t rxLen) {
    bool ok;

    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET);
    (void)BSP_flashXfer_(cmd);

    ok = BSP_flashWriteBlock_(tx, txLen);
    if (ok) {
        ok = BSP_flashReadBlock_(rx, rxLen);
    }

    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);

    return ok;
}
//............................................................................
// Status register 1: a blank part reads 0x00, all ones means nothing at all
// answered.
static uint8_t BSP_flashReadStatus_(void) {
    uint8_t status;

    BSP_flashExchange_(FLASH_STATUS_CMD, (uint8_t const *)0, 0U, &status, 1U);

    return status;
}
//............................................................................
// One sector: erase it, confirm it reads blank, program a signature carrying
// the sector index, read that back and compare. The index makes the pattern
// position dependent, which is what catches an addressing fault - the SRAM
// line walk did the same job.
// The write path self test (records 34/35/36, its own "fwt" task) is gone as
// of 2026-10-10. It was the second bus user this project documents as a
// conflict, and with an asset in the part the sweep would erase it. What it
// covered is covered elsewhere: the bootloader's burn reads every block back,
// and the run time write path was verified byte for byte (68/69/74/75, ids
// retired). Its shape - a static task, one step per pass, a yield between
// steps, park at the end - is recorded in docs/charter.md, for the background
// erase pacer when it comes.

static bool BSP_flashWriteEnable_(void) {
    BSP_flashExchange_(FLASH_WREN_CMD, (uint8_t const *)0, 0U,
                       (uint8_t *)0, 0U);

    return ((BSP_flashReadStatus_() & 0x02U) != 0U);
}
//............................................................................
// Start one aligned sector erase. Completion is polled by the wait API;
// a misaligned address would erase the surrounding sector, so reject it.
static bool BSP_flashEraseSectorStart_(uint32_t address) {
    uint8_t addr[3];

    BSP_flashAddr_(addr, address);

    if ((address % FLASH_DEV_SECTOR) != 0U) {
        return false;
    }

    if (!BSP_flashWriteEnable_()) {
        return false;
    }

    BSP_flashExchange_(FLASH_SECTOR_CMD, addr, 3U, (uint8_t *)0, 0U);

    return true;
}
//............................................................................

//============================================================================
// Wait API: scheduling capabilities are supplied by the caller.
#define FLASH_WAIT_DMA_MS 200U
#define FLASH_WAIT_DEVICE_MS 3000U
#define FLASH_WAIT_ERASE_STEP_MS 2U
#define FLASH_WAIT_PROGRAM_STEP_MS 1U

SM_DEFINE_MODULE("flashwait")

static BSP_flashWaitOps flashWait_;
static bool flashWaitBound_;

static void BSP_flashWaitRequireOwner_(void) {
    SM_REQUIRE(flashWaitBound_);
    SM_REQUIRE(flashWait_.isCaller(flashWait_.context));
}

void BSP_flashBindWait(BSP_flashWaitOps const *ops) {
    SM_REQUIRE(!flashWaitBound_);
    SM_REQUIRE(ops != (BSP_flashWaitOps const *)0);
    SM_REQUIRE(ops->isCaller != 0);
    SM_REQUIRE(ops->waitDone != 0);
    SM_REQUIRE(ops->notifyDone != 0);
    SM_REQUIRE(ops->sleepMs != 0);
    SM_REQUIRE(ops->isCaller(ops->context));
    flashWait_ = *ops;
    flashWaitBound_ = true;
}

// DMA completion is an ISR; a refused or failed start calls back inline.
static void BSP_flashWaitDone_(bool ok) {
    (void)ok; // flashDmaFailed_ holds the transfer result.
    flashWait_.notifyDone(flashWait_.context);
}

static bool BSP_flashWaitDma_(void) {
    if (!flashWait_.waitDone(flashWait_.context, FLASH_WAIT_DMA_MS)) {
        // Do not return a buffer that the DMA may still be using.
        SM_onAssert("fdma", __LINE__);
        return false;
    }
    return !flashDmaFailed_;
}

static bool BSP_flashWaitDevice_(uint32_t stepMs) {
    uint32_t const started = HAL_GetTick();

    while (!BSP_flashIsReady_()) {
        if ((uint32_t)(HAL_GetTick() - started) >= FLASH_WAIT_DEVICE_MS) {
            return false;
        }
        flashWait_.sleepMs(flashWait_.context, stepMs);
    }
    return true;
}

bool BSP_flashReadWait(uint32_t address, void *dst, uint32_t len) {
    uint8_t *out = (uint8_t *)dst;

    BSP_flashWaitRequireOwner_();
    while (len > 0U) {
        uint32_t const n = (len > FLASH_DMA_CHUNK) ? FLASH_DMA_CHUNK : len;

        BSP_flashReadDma_(address, out, n, &BSP_flashWaitDone_);
        if (!BSP_flashWaitDma_()) {
            return false;
        }
        address += n;
        out += n;
        len -= n;
    }
    return true;
}

bool BSP_flashWriteWait(uint32_t address, void const *src, uint32_t len) {
    uint8_t const *in = (uint8_t const *)src;

    BSP_flashWaitRequireOwner_();
    while (len > 0U) {
        uint32_t const room = FLASH_DEV_PAGE - (address % FLASH_DEV_PAGE);
        uint32_t const n = (len > room) ? room : len;

        BSP_flashProgramPageDma_(address, in, n, &BSP_flashWaitDone_);
        if (!BSP_flashWaitDma_()
            || !BSP_flashWaitDevice_(FLASH_WAIT_PROGRAM_STEP_MS)) {
            return false;
        }
        address += n;
        in += n;
        len -= n;
    }
    return true;
}

bool BSP_flashEraseSectorWait(uint32_t address) {
    BSP_flashWaitRequireOwner_();
    if (!BSP_flashEraseSectorStart_(address)) {
        return false;
    }
    return BSP_flashWaitDevice_(FLASH_WAIT_ERASE_STEP_MS);
}
