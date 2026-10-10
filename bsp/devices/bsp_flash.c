//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "bsp.h"
#include "bsp_flash.h"

#include <string.h>

//============================================================================
#define FLASH_CS_PORT   GPIOB
#define FLASH_CS_PIN    GPIO_PIN_12

#define FLASH_JEDEC_CMD     0x9FU
#define FLASH_STATUS_CMD    0x05U
#define FLASH_READ_CMD      0x03U
#define FLASH_WREN_CMD      0x06U
#define FLASH_PROGRAM_CMD   0x02U
#define FLASH_SECTOR_CMD    0x20U
#define FLASH_JEDEC_W25Q128 0xEF4018UL // EFh + device 4018h, spec 7.2.1
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

// Geometry and the two waits that matter: a 4 KB sector erase is 45 ms
// typical and 400 ms worst case, a 256 byte page program 0.7 ms / 3 ms.
// (not FLASH_PAGE_SIZE: the HAL already uses that name for the internal
//  flash page size, and redefining it would lie to the HAL's own driver)
#define FLASH_SECTOR_SIZE   4096UL
#define FLASH_SECTORS       4096U // 16 MB / 4 KB
#define FLASH_SIZE          (FLASH_SECTOR_SIZE * FLASH_SECTORS)
#define FLASH_ERASE_MS      2000U
#define FLASH_PROGRAM_MS    100U

// Signature written by the self test: the sector index in it says where the
// bytes landed, which is what catches an addressing fault (the SRAM line walk
// did the same job). Sixteen bytes never cross a page on a 4 KB aligned
// start.
#define FLASH_MARK_BYTES    16U

// Operating clock: PCLK1 / 2 = 18 MHz, the fastest the peripheral offers. A
// sweep of all four dividers (2.25, 4.5, 9, 18 MHz) came back clean at every
// step - sixteen reads of the same address per clock, all identical and all
// matching each other - so there is nothing to be gained by running slower.
#define FLASH_SPI_PRESCALER SPI_BAUDRATEPRESCALER_2

// Trace records: ==FLASH==[JEDEC][<mfr> <type> <cap>]==[W25Q128/??????]== and
// ==FLASH==[ARRAY][<six bytes at address 0>]==. The second is the "this is
// memory and it reads" line; the first one is identity only.
#define FLASH_TRACE_ID_OK   25U
#define FLASH_TRACE_ID_BAD  26U
#define FLASH_TRACE_ARRAY   32U

// Write path self test: temporary, and destructive - it erases every sector.
//   34 ==FLASH==[WRTEST][start][sectors <n>]==
//   35 ==FLASH==[WRTEST][at <n>]==              progress, every 512 sectors
//   36 ==FLASH==[WRTEST][done][ok][eraseBad][progBad]==
#define FLASH_TRACE_WT_START 34U
#define FLASH_TRACE_WT_AT    35U
#define FLASH_TRACE_WT_DONE  36U
#define FLASH_WT_REPORT_EVERY 512U
// Each erase failure costs the full 2 s timeout, so a broken erase path would
// turn the sweep into hours. Give up early instead and let the counts say so.
#define FLASH_WT_MAX_ERASE_BAD 8U
// The self test runs in its own task, not in the AO: time events go out every
// period regardless of consumption (SST_TimeEvt_tick), so a handler slower
// than the beat fills the AO's 8 slot queue and trips DBC_REQUIRE(300) in
// SST_Task_post. A plain task has no such queue and may block, which is what
// a multi minute flash sweep needs.
#define FLASH_WT_STACK_WORDS   256U
#define FLASH_WT_PRIORITY      20U // under Blinky (24) and the UI thread (23)

// How much of the part to walk. Erase is ~130 ms per sector here, against a
// datasheet typical of 45 ms, so the full walk takes about ten minutes and
// the sample walk - every 64th sector, spread over the whole 16 MB - gives a
// verdict in about ten seconds. Set this to OFF once the flash holds data
// worth keeping: the walk erases whatever it touches. OFF since the flash was
// signed off: it erases the part on every boot, and it drives SPI2 from its
// own task while the flash thread does too - two writers on one bus is a
// conflict this project already documents, and there is nothing left for it
// to prove.
#define FLASH_WT_OFF    0U
#define FLASH_WT_SAMPLE 1U
#define FLASH_WT_FULL   2U
#define FLASH_WT_MODE   FLASH_WT_OFF
#define FLASH_WT_STRIDE 64U

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
static bool BSP_flashWriteBlock_(uint8_t const *tx, uint32_t len);
static bool BSP_flashReadBlock_(uint8_t *rx, uint32_t len);
static bool BSP_flashExchange_(uint8_t cmd, uint8_t const *tx, uint32_t txLen,
                               uint8_t *rx, uint32_t rxLen);
static uint32_t BSP_flashReadId_(void);
static uint8_t BSP_flashReadStatus_(void);
static void BSP_flashReadArray_(uint8_t *out, uint8_t count,
                                uint32_t address);
static bool BSP_flashWriteEnable_(void);
static bool BSP_flashWaitReady_(uint32_t timeoutMs);
static bool BSP_flashEraseSector_(uint32_t address);
static bool BSP_flashProgram_(uint32_t address, uint8_t const *data,
                              uint32_t len);
static bool BSP_flashTestSector_(uint16_t sector);
static uint16_t BSP_flashWtCount_(void);
static void BSP_flashTrace3_(uint8_t recId, uint32_t id);

// Self test state: it is stepped from the AO, a few sectors per beat.
static uint16_t flashWtSector_;
static uint16_t flashWtOk_;
static uint16_t flashWtEraseBad_;
static uint16_t flashWtProgBad_;
static bool flashWtRunning_;
static StaticTask_t flashWtTcb_;
static StackType_t flashWtStack_[FLASH_WT_STACK_WORDS];

static void BSP_flashSelfTestTask_(void *arg);

//============================================================================
// DMA read path, for the run time. The boot window cannot use it: before the
// scheduler the tracer's critical section leaves interrupts disabled (see
// AGENTS.md), so an interrupt driven transfer has no way to complete there.
// Asset_load() therefore uses the blocking BSP_flashRead() above, and this
// path serves the flash thread, which has a scheduler to block on.
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
void BSP_flashProbe(void) {
    uint32_t const id = BSP_flashReadId_();
    uint8_t array[6];

    BSP_flashTrace3_((id == FLASH_JEDEC_W25Q128) ? FLASH_TRACE_ID_OK
                                                 : FLASH_TRACE_ID_BAD, id);

    // Read data from address 0: the functional check that this is memory, and
    // six bytes so a boot sector's OEM name shows up.
    BSP_flashReadArray_(array, 6U, 0U);
    SM_TRACE_BEGIN(FLASH_TRACE_ARRAY, 6)
        SM_TRACE_PAYLOAD_BE(array[0])
        SM_TRACE_PAYLOAD_BE(array[1])
        SM_TRACE_PAYLOAD_BE(array[2])
        SM_TRACE_PAYLOAD_BE(array[3])
        SM_TRACE_PAYLOAD_BE(array[4])
        SM_TRACE_PAYLOAD_BE(array[5])
    SM_TRACE_END()
}
//............................................................................
// Read Data (03h) over an arbitrary length. The device increments the
// address by itself for as long as the clock keeps coming, so one
// transaction covers the whole block. No overrun to clean up either: the
// byte-wise exchange reads the receive register on every unit, so RXNE
// never stays set.
// The two write primitives the asset burner needs, exposed for the bootloader
// (the app's own write path stays the wear self test). Thin wrappers: the
// ordering rules - write enable, wait for the device, no page crossing - are
// the private ones' business, and they are the ones already exercised.
bool BSP_flashEraseSector(uint32_t address) {
    return BSP_flashEraseSector_(address);
}
//............................................................................
bool BSP_flashProgramPage(uint32_t address, uint8_t const *data,
                          uint32_t len) {
    // One page at most, and never across a boundary: the device would
    // wrap and silently program the start of the page again.
    if ((len == 0U) || (len > FLASH_DEV_PAGE)
        || ((address % FLASH_DEV_PAGE) + len > FLASH_DEV_PAGE)) {
        return false;
    }

    return BSP_flashProgram_(address, data, len);
}
//............................................................................
bool BSP_flashRead(uint32_t address, uint8_t *dst, uint32_t len) {
    uint8_t const addr[3] = {
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)address
    };

    if ((dst == (uint8_t *)0) || (len == 0U) || (address >= FLASH_SIZE)
        || (len > (FLASH_SIZE - address))) {
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
void BSP_flashReadDma(uint32_t address, uint8_t *dst, uint32_t len,
                      BSP_flashDoneCb done) {
    uint8_t const addr[3] = {
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)address
    };

    if ((dst == (uint8_t *)0) || (len == 0U) || (len > FLASH_DMA_CHUNK)
        || flashDmaBusy_ || (address >= FLASH_SIZE)
        || (len > (FLASH_SIZE - address))) {
        // A refusal is reported the way a failed transfer is, so a caller
        // that waits on the callback always reads the truth out of
        // BSP_flashDmaFailed().
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
bool BSP_flashDmaBusy(void) {
    return flashDmaBusy_;
}
//............................................................................
bool BSP_flashDmaFailed(void) {
    return flashDmaFailed_;
}
//............................................................................
// One page, moved by DMA: the write side of BSP_flashReadDma above, for a run
// time writer. One arm is one transaction (write enable, then command and
// address from the CPU, the payload from the channel, chip select raised in
// the completion interrupt).
//
// The device is still programming when that interrupt fires - tPP is 0.7 to
// 3 ms - so the caller waits afterwards with BSP_flashIsReady(). That is
// deliberate: polling the device from the interrupt would hold the CPU for
// milliseconds inside an ISR, and the caller is where the yield belongs.
void BSP_flashProgramPageDma(uint32_t address, uint8_t const *data,
                             uint32_t len, BSP_flashDoneCb done) {
    uint8_t const addr[3] = {
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)address
    };

    // Same page guard as the blocking BSP_flashProgramPage: the device wraps
    // inside a page and would silently program the start of it again.
    if ((data == (uint8_t const *)0) || (len == 0U)
        || (len > FLASH_DEV_PAGE)
        || ((address % FLASH_DEV_PAGE) + len > FLASH_DEV_PAGE)
        || flashDmaBusy_ || (address >= FLASH_SIZE)
        || (len > (FLASH_SIZE - address))) {
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
// One poll of the status register: the caller owns how long to wait and
// whether to yield while it does. The waiting policy stays out of this file
// because the bootloader links it too, and the bootloader has no scheduler
// (and no FreeRTOS) to delay on.
bool BSP_flashIsReady(void) {
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
// Read Data (03h): three address bytes, then the array comes back.
static void BSP_flashReadArray_(uint8_t *out, uint8_t count,
                                uint32_t address) {
    uint8_t const addr[3] = {
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)address
    };

    BSP_flashExchange_(FLASH_READ_CMD, addr, 3U, out, count);
}
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
// JEDEC ID: manufacturer, memory type, capacity.
static uint32_t BSP_flashReadId_(void) {
    uint8_t id[3];

    BSP_flashExchange_(FLASH_JEDEC_CMD, (uint8_t const *)0, 0U, id, 3U);

    return ((uint32_t)id[0] << 16U) | ((uint32_t)id[1] << 8U)
           | (uint32_t)id[2];
}
// Status register 1: a blank part reads 0x00, all ones means nothing at all
// answered.
static uint8_t BSP_flashReadStatus_(void) {
    uint8_t status;

    BSP_flashExchange_(FLASH_STATUS_CMD, (uint8_t const *)0, 0U, &status, 1U);

    return status;
}
//............................................................................
static void BSP_flashTrace3_(uint8_t recId, uint32_t id) {
    SM_TRACE_BEGIN(recId, 3)
        SM_TRACE_PAYLOAD_BE(id >> 16)
        SM_TRACE_PAYLOAD_BE(id >> 8)
        SM_TRACE_PAYLOAD_BE(id)
    SM_TRACE_END()
}
//............................................................................
// One sector: erase it, confirm it reads blank, program a signature carrying
// the sector index, read that back and compare. The index makes the pattern
// position dependent, which is what catches an addressing fault - the SRAM
// line walk did the same job.
static bool BSP_flashTestSector_(uint16_t sector) {
    uint32_t const base = (uint32_t)sector * FLASH_SECTOR_SIZE;
    uint8_t want[FLASH_MARK_BYTES];
    uint8_t got[FLASH_MARK_BYTES];
    uint8_t i;
    bool erased;

    want[0] = (uint8_t)(sector >> 8U);
    want[1] = (uint8_t)sector;
    want[2] = 0x5AU;
    want[3] = 0xA5U;
    for (i = 4U; i < FLASH_MARK_BYTES; ++i) {
        want[i] = (uint8_t)(0x30U + i);
    }

    erased = BSP_flashEraseSector_(base);
    if (erased) {
        BSP_flashReadArray_(got, FLASH_MARK_BYTES, base);
        for (i = 0U; i < FLASH_MARK_BYTES; ++i) {
            if (got[i] != 0xFFU) {
                erased = false;
            }
        }
    }

    if (!erased) {
        ++flashWtEraseBad_;
        return false;
    }

    if (!BSP_flashProgram_(base, want, FLASH_MARK_BYTES)) {
        ++flashWtProgBad_;
        return false;
    }

    BSP_flashReadArray_(got, FLASH_MARK_BYTES, base);
    if (memcmp(want, got, FLASH_MARK_BYTES) == 0) {
        ++flashWtOk_;
        return true;
    }

    ++flashWtProgBad_;
    return false;
}
//............................................................................
// How many sectors this run touches: the start and done records count on it.
static uint16_t BSP_flashWtCount_(void) {
    uint16_t const stride = (FLASH_WT_MODE == FLASH_WT_FULL)
                            ? 1U : FLASH_WT_STRIDE;

    return (uint16_t)((FLASH_SECTORS + stride - 1U) / stride);
}
//............................................................................
void BSP_flashSelfTestStart(void) {
    if (flashWtRunning_ || (FLASH_WT_MODE == FLASH_WT_OFF)) {
        return; // one sweep per boot
    }

    flashWtSector_ = 0U;
    flashWtOk_ = 0U;
    flashWtEraseBad_ = 0U;
    flashWtProgBad_ = 0U;
    flashWtRunning_ = true;

    // Status register first: a set block protect bit would make every erase
    // fail, and that is worth knowing before waiting eleven minutes.
    {
        uint16_t const count = BSP_flashWtCount_();

        SM_TRACE_BEGIN(FLASH_TRACE_WT_START, 3)
            SM_TRACE_PAYLOAD_BE(BSP_flashReadStatus_())
            SM_TRACE_PAYLOAD_BE(count >> 8)
            SM_TRACE_PAYLOAD_BE(count)
        SM_TRACE_END()
    }

    (void)xTaskCreateStatic(BSP_flashSelfTestTask_, "fwt",
                            FLASH_WT_STACK_WORDS, (void *)0,
                            FLASH_WT_PRIORITY,
                            flashWtStack_, &flashWtTcb_);
}
//............................................................................
// Erase, check blank, program a signature, read it back: one sector per pass,
// with a one tick delay between sectors so the idle task keeps draining the
// tracer while this runs for minutes.
static void BSP_flashSelfTestTask_(void *arg) {
    uint16_t sector;

    uint16_t const stride = (FLASH_WT_MODE == FLASH_WT_FULL)
                            ? 1U : FLASH_WT_STRIDE;

    (void)arg;

    for (sector = 0U; sector < (uint16_t)FLASH_SECTORS; sector += stride) {
        (void)BSP_flashTestSector_(sector);
        flashWtSector_ = sector;

        if ((sector % FLASH_WT_REPORT_EVERY) == 0U) {
            SM_TRACE_BEGIN(FLASH_TRACE_WT_AT, 2)
                SM_TRACE_PAYLOAD_BE(sector >> 8)
                SM_TRACE_PAYLOAD_BE(sector)
            SM_TRACE_END()
        }

        if (flashWtEraseBad_ >= FLASH_WT_MAX_ERASE_BAD) {
            break;
        }

        // WAIT: one tick between sectors. The erase and program above spin on
        // BUSY, so this is the only place the self test lets idle run - that
        // is what keeps the tracer alive during a ten minute sweep.
        vTaskDelay(1U);
    }

    SM_TRACE_BEGIN(FLASH_TRACE_WT_DONE, 6)
        SM_TRACE_PAYLOAD_BE(flashWtOk_ >> 8)
        SM_TRACE_PAYLOAD_BE(flashWtOk_)
        SM_TRACE_PAYLOAD_BE(flashWtEraseBad_ >> 8)
        SM_TRACE_PAYLOAD_BE(flashWtEraseBad_)
        SM_TRACE_PAYLOAD_BE(flashWtProgBad_ >> 8)
        SM_TRACE_PAYLOAD_BE(flashWtProgBad_)
    SM_TRACE_END()

    // This task only exists while FLASH_WT_MODE is not OFF, so this report is
    // only reachable during a self test run: it is here so a re-enabled sweep
    // measures its own stack instead of hiding it (the flash thread reports
    // through BSP_TRACE_STACK_FLASH).
    BSP_memReport(BSP_TRACE_STACK_FLASH_WT);

    for (;;) { // static task: park instead of deleting
        vTaskDelay(1000U);
    }
}
//............................................................................
// Write Enable (06h), confirmed by reading the status register back: WEL is
// bit 1, and a write or erase is ignored without it.
static bool BSP_flashWriteEnable_(void) {
    BSP_flashExchange_(FLASH_WREN_CMD, (uint8_t const *)0, 0U,
                       (uint8_t *)0, 0U);

    return ((BSP_flashReadStatus_() & 0x02U) != 0U);
}
//............................................................................
// Poll BUSY until it clears. Timed off the HAL tick, so it behaves the same
// before and after the scheduler starts.
//
// SPIN: a deadline loop, not a block - the CPU is held for as long as the
// device needs (a sector erase measures ~130 ms here, up to FLASH_ERASE_MS).
// The self test's vTaskDelay(1) per sector is what gives idle a chance to
// keep pumping the tracer; a WAIT here would come with the DMA/IRQ path.
static bool BSP_flashWaitReady_(uint32_t timeoutMs) {
    uint32_t const deadline = HAL_GetTick() + timeoutMs;

    for (;;) {
        if ((BSP_flashReadStatus_() & 0x01U) == 0U) {
            return true;
        }
        if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
            return false;
        }
    }
}
//............................................................................
// Start one sector erase and return: the device is erasing when this comes
// back, so the caller polls BSP_flashIsReady() for as long as it likes. That
// split is what lets a run time caller sleep through the device's time
// instead of spinning in it (see sm_flash.c); the blocking form below is this
// plus the spin, which is what the bootloader needs - it has no scheduler.
//
// The alignment guard is here and not in the old blocking path: a sector
// erase command with any other address erases the sector that contains it,
// which is a silent way to lose data next door.
bool BSP_flashEraseSectorStart(uint32_t address) {
    uint8_t const addr[3] = {
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)address
    };

    if ((address % FLASH_SECTOR_SIZE) != 0U) {
        return false;
    }

    if (!BSP_flashWriteEnable_()) {
        return false;
    }

    BSP_flashExchange_(FLASH_SECTOR_CMD, addr, 3U, (uint8_t *)0, 0U);

    return true;
}
//............................................................................
static bool BSP_flashEraseSector_(uint32_t address) {
    return BSP_flashEraseSectorStart(address)
           && BSP_flashWaitReady_(FLASH_ERASE_MS);
}
//............................................................................
static bool BSP_flashProgram_(uint32_t address, uint8_t const *data,
                              uint32_t len) {
    uint8_t const addr[3] = {
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)address
    };
    uint32_t i;

    if (!BSP_flashWriteEnable_()) {
        return false;
    }

    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET);
    (void)BSP_flashXfer_(FLASH_PROGRAM_CMD);
    for (i = 0U; i < 3U; ++i) {
        (void)BSP_flashXfer_(addr[i]);
    }
    for (i = 0U; i < len; ++i) {
        (void)BSP_flashXfer_(data[i]);
    }
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);

    return BSP_flashWaitReady_(FLASH_PROGRAM_MS);
}
//............................................................................
