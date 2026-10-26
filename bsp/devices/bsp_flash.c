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

static SPI_HandleTypeDef flash_spi_;

//============================================================================
static void BSP_flashGpioInit_(void);
static void BSP_flashSpiInit_(void);
// Keep the last block-transfer outcome for the debugger, using HAL's status
// values: failed transfers must not look like successful unchanged buffers.
static uint8_t flashTxStatus_ = 0xFFU;
static uint8_t flashRxStatus_ = 0xFFU;

static bool BSP_flashWaitFlag_(uint32_t flag, bool set);
static bool BSP_flashTransfer_(uint8_t const *tx, uint8_t *rx, uint32_t len);
static bool BSP_flashXfer_(uint8_t out, uint8_t *in);
static void BSP_flashAddr_(uint8_t out[3], uint32_t address);
static bool BSP_flashWriteBlock_(uint8_t const *tx, uint32_t len);
static bool BSP_flashReadBlock_(uint8_t *rx, uint32_t len);
static bool BSP_flashBegin_(uint8_t cmd, uint8_t const *addr);
static bool BSP_flashExchange_(uint8_t cmd, uint8_t const *addr,
                               uint8_t const *tx, uint8_t *rx, uint32_t len);
static bool BSP_flashProgramBegin_(uint32_t address);
static bool BSP_flashReadStatus_(uint8_t *status);
static bool BSP_flashWriteEnable_(void);


SM_DEFINE_MODULE("flash")

static void BSP_flashDmaInit_(void);
static SPI_HandleTypeDef * const flashDeviceContext_ = &flash_spi_;
static BSP_Flash flashDevice_ = {.device = &flash_spi_};

// Hardware channel state belongs to this single SPI2 device. The completion
// target is latched for the duration of a DMA, never looked up after rebind.
#define FLASH_DMA_IRQ_PRIO 5U
#define FLASH_WAIT_TRANSFER_MS 200U
#define FLASH_WAIT_DEVICE_MS 3000U
#define FLASH_ERASE_STEP_MS 2U
#define FLASH_PROGRAM_STEP_MS 1U

static DMA_HandleTypeDef flashDmaRx_;
static DMA_HandleTypeDef flashDmaTx_;
static volatile bool flashDmaBusy_;
static BSP_flashDone flashDmaDone_;
static void *flashDmaContext_;

//============================================================================
// HARDWARE INITIALIZATION -- no application wait capabilities needed.
void BSP_flashInit(void) {
    BSP_flashGpioInit_();
    BSP_flashSpiInit_();
    BSP_flashDmaInit_();

    // Throwaway read. The first transaction after power-up came back wrong on
    // every boot we measured (three boots, three answers) while every later
    // one matched, so the first one is spent here instead of on data.
    uint8_t status;
    (void)BSP_flashReadStatus_(&status);
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
//============================================================================
// SHARED PROTOCOL -- used by both operation tables.
// Shared synchronous SPI path for command/address/status and CPU payloads.
// Both App transfers and bootloader burn mode run with TIM1 tick active.
// These short SPI flag waits use the same HAL timebase in either phase.
static bool BSP_flashWaitFlag_(uint32_t flag, bool set) {
    uint32_t const started = HAL_GetTick();

    while (((flash_spi_.Instance->SR & flag) != 0U) != set) {
        if ((uint32_t)(HAL_GetTick() - started) >= FLASH_XFER_MS) {
            return false;
        }
    }
    return true;
}

static bool BSP_flashTransfer_(uint8_t const *tx, uint8_t *rx, uint32_t len) {
    for (uint32_t i = 0U; i < len; ++i) {
        if (!BSP_flashWaitFlag_(SPI_SR_TXE, true)) {
            return false;
        }
        *(__IO uint8_t *)&flash_spi_.Instance->DR = (tx != 0) ? tx[i] : 0xFFU;
        if (!BSP_flashWaitFlag_(SPI_SR_RXNE, true)) {
            return false;
        }
        uint8_t const in = *(__IO uint8_t *)&flash_spi_.Instance->DR;
        if (rx != 0) {
            rx[i] = in;
        }
    }
    return (len == 0U) || BSP_flashWaitFlag_(SPI_SR_BSY, false);
}

static bool BSP_flashXfer_(uint8_t out, uint8_t *in) {
    return BSP_flashTransfer_(&out, in, 1U);
}
//............................................................................
static void BSP_flashAddr_(uint8_t out[3], uint32_t address) {
    out[0] = (uint8_t)(address >> 16);
    out[1] = (uint8_t)(address >> 8);
    out[2] = (uint8_t)address;
}
//............................................................................
static bool BSP_flashWriteBlock_(uint8_t const *tx, uint32_t len) {
    bool const ok = BSP_flashTransfer_(tx, 0, len);

    flashTxStatus_ = ok ? (uint8_t)HAL_OK : (uint8_t)HAL_TIMEOUT;
    return ok;
}
//............................................................................
static bool BSP_flashReadBlock_(uint8_t *rx, uint32_t len) {
    bool const ok = BSP_flashTransfer_(0, rx, len);

    flashRxStatus_ = ok ? (uint8_t)HAL_OK : (uint8_t)HAL_TIMEOUT;
    return ok;
}
//............................................................................
// Open one command, optionally followed by its three address bytes. The
// caller closes CS after its CPU payload or from its DMA completion path,
// including failures while sending this shared header.
static bool BSP_flashBegin_(uint8_t cmd, uint8_t const *addr) {
    uint8_t in;

    __HAL_SPI_ENABLE(&flash_spi_);
    __HAL_SPI_CLEAR_OVRFLAG(&flash_spi_);
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET);
    return BSP_flashXfer_(cmd, &in)
           && ((addr == 0) || BSP_flashWriteBlock_(addr, 3U));
}
//............................................................................
// CPU payload in one direction, under the same CS as command and address.
static bool BSP_flashExchange_(uint8_t cmd, uint8_t const *addr,
                               uint8_t const *tx, uint8_t *rx, uint32_t len) {
    bool ok = BSP_flashBegin_(cmd, addr);

    if (ok) {
        ok = (tx != 0) ? BSP_flashWriteBlock_(tx, len)
                       : BSP_flashReadBlock_(rx, len);
    }
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);
    return ok;
}
//............................................................................
static bool BSP_flashReadStatus_(uint8_t *status) {
    return BSP_flashExchange_(FLASH_STATUS_CMD, 0, 0, status, 1U);
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
    uint8_t status;

    return BSP_flashExchange_(FLASH_WREN_CMD, 0, 0, 0, 0U)
           && BSP_flashReadStatus_(&status) && ((status & 0x02U) != 0U);
}
//............................................................................
// CPU and DMA page programming share write-enable and command/address setup.
static bool BSP_flashProgramBegin_(uint32_t address) {
    uint8_t addr[3];

    BSP_flashAddr_(addr, address);
    return BSP_flashWriteEnable_()
           && BSP_flashBegin_(FLASH_PROGRAM_CMD, addr);
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

    return BSP_flashExchange_(FLASH_SECTOR_CMD, addr, 0, 0, 0U);
}
//............................................................................

//============================================================================
// POLLING OPERATIONS -- CPU transfers, inline completion, no DMA IRQ.
static void BSP_flashReadPoll_(void *device, uint32_t address, void *dst,
                               uint32_t len, BSP_flashDone done,
                               void *context) {
    uint8_t addr[3];

    SM_REQUIRE(device == flashDeviceContext_);
    SM_REQUIRE(flashDevice_.busy);
    BSP_flashAddr_(addr, address);
    bool const ok = BSP_flashExchange_(FLASH_READ_CMD, addr, 0, dst, len);

    done(context, ok);
}

static void BSP_flashProgramPoll_(void *device, uint32_t address,
                                  void const *src, uint32_t len,
                                  BSP_flashDone done, void *context) {
    SM_REQUIRE(device == flashDeviceContext_);
    SM_REQUIRE(flashDevice_.busy);
    bool ok = BSP_flashProgramBegin_(address);

    if (ok) {
        ok = BSP_flashWriteBlock_(src, len);
    }
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);
    done(context, ok);
}

static void BSP_flashErasePoll_(void *device, uint32_t address,
                                BSP_flashDone done, void *context) {
    SM_REQUIRE(device == flashDeviceContext_);
    SM_REQUIRE(flashDevice_.busy);
    done(context, BSP_flashEraseSectorStart_(address));
}

BSP_flashOperations const BSP_flashPollOperations = {
    .read = &BSP_flashReadPoll_,
    .program = &BSP_flashProgramPoll_,
    .erase = &BSP_flashErasePoll_,
};

//============================================================================
// DMA OPERATIONS -- headers/status share the polling helpers above; read and
// program payloads use channels 4/5. Interrupts must be live. One arm is one
// transaction; never rearm inside HAL callbacks (HAL still owns its handles).
static void BSP_flashDmaFinish_(bool ok) {
    BSP_flashDone const done = flashDmaDone_;
    void * const context = flashDmaContext_;

    SM_REQUIRE(flashDmaBusy_);
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);
    flashDmaDone_ = (BSP_flashDone)0;
    flashDmaContext_ = (void *)0;
    flashDmaBusy_ = false;
    done(context, ok);
}

static void BSP_flashReadDma_(void *device, uint32_t address, void *dst,
                              uint32_t len, BSP_flashDone done,
                              void *context) {
    uint8_t addr[3];

    SM_REQUIRE(device == flashDeviceContext_);
    SM_REQUIRE(flashDevice_.busy);
    SM_REQUIRE(!flashDmaBusy_);
    BSP_flashAddr_(addr, address);
    flashDmaDone_ = done;
    flashDmaContext_ = context;
    flashDmaBusy_ = true;

    if (!BSP_flashBegin_(FLASH_READ_CMD, addr)
        || (HAL_SPI_Receive_DMA(&flash_spi_, dst, (uint16_t)len) != HAL_OK)) {
        BSP_flashDmaFinish_(false);
    }
}

static void BSP_flashProgramDma_(void *device, uint32_t address,
                                 void const *src, uint32_t len,
                                 BSP_flashDone done, void *context) {
    SM_REQUIRE(device == flashDeviceContext_);
    SM_REQUIRE(flashDevice_.busy);
    SM_REQUIRE(!flashDmaBusy_);
    flashDmaDone_ = done;
    flashDmaContext_ = context;
    flashDmaBusy_ = true;

    if (!BSP_flashProgramBegin_(address)
        || (HAL_SPI_Transmit_DMA(&flash_spi_, (uint8_t *)(uintptr_t)src,
                                 (uint16_t)len) != HAL_OK)) {
        BSP_flashDmaFinish_(false);
    }
}

// Erase has no payload to DMA. Both tables send the same sector command;
// chip BUSY is waited out by the object's bound waiting operations.
BSP_flashOperations const BSP_flashDmaOperations = {
    .read = &BSP_flashReadDma_,
    .program = &BSP_flashProgramDma_,
    .erase = &BSP_flashErasePoll_,
};

// HAL master two-line receive completes through RxCplt, not TxRxCplt.
// HAL finishes TXE/BSY and clears write-side OVR before these callbacks.
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi == &flash_spi_) {
        BSP_flashDmaFinish_(true);
    }
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi == &flash_spi_) {
        BSP_flashDmaFinish_(true);
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi) {
    if (hspi == &flash_spi_) {
        BSP_flashDmaFinish_(false);
    }
}

void DMA1_Channel4_IRQHandler(void) {
    HAL_DMA_IRQHandler(&flashDmaRx_);
}

void DMA1_Channel5_IRQHandler(void) {
    HAL_DMA_IRQHandler(&flashDmaTx_);
}

//============================================================================
// OBJECT BINDING AND PUBLIC METHODS -- complete synchronous Flash operations.
BSP_Flash *BSP_flashDevice(void) {
    return &flashDevice_;
}

void BSP_flashBind(BSP_Flash *me, BSP_flashOperations const *operations,
                   BSP_flashWaitOperations const *waitOperations,
                   void *waitContext) {
    SM_REQUIRE(me == &flashDevice_);
    SM_REQUIRE(__get_IPSR() == 0U);
    SM_REQUIRE(!me->busy && !me->complete && !flashDmaBusy_);
    SM_REQUIRE(operations != (BSP_flashOperations const *)0);
    SM_REQUIRE(operations->read != 0);
    SM_REQUIRE(operations->program != 0);
    SM_REQUIRE(operations->erase != 0);
    SM_REQUIRE(waitOperations != (BSP_flashWaitOperations const *)0);
    SM_REQUIRE(waitOperations->isCaller != 0);
    SM_REQUIRE(waitOperations->waitDone != 0);
    SM_REQUIRE(waitOperations->notifyDone != 0);
    SM_REQUIRE(waitOperations->sleepMs != 0);
    SM_REQUIRE(waitOperations->yield != 0);
    SM_REQUIRE(waitOperations->now != 0);
    SM_REQUIRE(waitOperations->elapsedMs != 0);
    if (me->waitOperations != (BSP_flashWaitOperations const *)0) {
        uint8_t status;

        SM_REQUIRE(me->waitOperations->isCaller(me->waitContext));
        // A previous device timeout may leave a program/erase in progress.
        SM_REQUIRE(BSP_flashReadStatus_(&status));
        SM_REQUIRE((status & 1U) == 0U);
    }
    me->operations = operations;
    me->waitOperations = waitOperations;
    me->waitContext = waitContext;
}

static void BSP_flashEnter_(BSP_Flash *me) {
    SM_REQUIRE(me == &flashDevice_);
    SM_REQUIRE(me->operations != (BSP_flashOperations const *)0);
    SM_REQUIRE(me->waitOperations != (BSP_flashWaitOperations const *)0);
    SM_REQUIRE(me->waitOperations->isCaller(me->waitContext));
    SM_REQUIRE(!me->busy && !me->complete && !flashDmaBusy_);
    me->busy = true;
}

// Inline or ISR: save the result before publishing the completion.
static void BSP_flashDone_(void *context, bool ok) {
    BSP_Flash * const me = context;

    SM_REQUIRE(me->busy && !me->complete);
    me->ok = ok;
    me->complete = true;
    me->waitOperations->notifyDone(me->waitContext);
}

static bool BSP_flashWaitTransfer_(BSP_Flash *me) {
    if (!me->waitOperations->waitDone(me->waitContext,
                                      FLASH_WAIT_TRANSFER_MS)) {
        // A lost completion cannot release a buffer still owned by DMA.
        SM_onAssert("fdma", __LINE__);
        return false;
    }
    SM_REQUIRE(me->complete);
    SM_REQUIRE(!flashDmaBusy_);
    bool const ok = me->ok;

    me->complete = false;
    return ok;
}

static bool BSP_flashWaitDevice_(BSP_Flash *me, uint32_t stepMs) {
    uint32_t const started = me->waitOperations->now(me->waitContext);
    uint8_t status;

    for (;;) {
        if (!BSP_flashReadStatus_(&status)) {
            return false;
        }
        if ((status & 1U) == 0U) {
            return true;
        }
        if (me->waitOperations->elapsedMs(me->waitContext, started)
            >= FLASH_WAIT_DEVICE_MS) {
            return false;
        }
        me->waitOperations->sleepMs(me->waitContext, stepMs);
    }
}

bool BSP_flashRead(BSP_Flash *me, uint32_t address, void *dst, uint32_t len) {
    uint8_t *out = dst;
    bool ok = true;

    BSP_flashEnter_(me);
    if (((dst == 0) && (len != 0U)) || (address > FLASH_DEV_SIZE)
        || (len > FLASH_DEV_SIZE - address)) {
        ok = false;
    }
    while (ok && (len > 0U)) {
        uint32_t const n = (len > FLASH_DMA_CHUNK) ? FLASH_DMA_CHUNK : len;

        me->operations->read(me->device, address, out, n,
                              &BSP_flashDone_, me);
        ok = BSP_flashWaitTransfer_(me);
        address += n;
        out += n;
        len -= n;
        if (ok && (len > 0U)) {
            me->waitOperations->yield(me->waitContext);
        }
    }
    me->busy = false;
    return ok;
}

bool BSP_flashWrite(BSP_Flash *me, uint32_t address,
                    void const *src, uint32_t len) {
    uint8_t const *in = src;
    bool ok = true;

    BSP_flashEnter_(me);
    if (((src == 0) && (len != 0U)) || (address > FLASH_DEV_SIZE)
        || (len > FLASH_DEV_SIZE - address)) {
        ok = false;
    }
    while (ok && (len > 0U)) {
        uint32_t const room = FLASH_DEV_PAGE - (address % FLASH_DEV_PAGE);
        uint32_t const n = (len > room) ? room : len;

        me->operations->program(me->device, address, in, n,
                                 &BSP_flashDone_, me);
        ok = BSP_flashWaitTransfer_(me)
             && BSP_flashWaitDevice_(me, FLASH_PROGRAM_STEP_MS);
        address += n;
        in += n;
        len -= n;
        if (ok && (len > 0U)) {
            me->waitOperations->yield(me->waitContext);
        }
    }
    me->busy = false;
    return ok;
}

bool BSP_flashEraseSector(BSP_Flash *me, uint32_t address) {
    bool ok = false;

    BSP_flashEnter_(me);
    if ((address < FLASH_DEV_SIZE) && ((address % FLASH_DEV_SECTOR) == 0U)) {
        me->operations->erase(me->device, address, &BSP_flashDone_, me);
        ok = BSP_flashWaitTransfer_(me)
             && BSP_flashWaitDevice_(me, FLASH_ERASE_STEP_MS);
    }
    me->busy = false;
    return ok;
}
