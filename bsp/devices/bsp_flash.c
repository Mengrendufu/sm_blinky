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

// Geometry and the two waits that matter: a 4 KB sector erase is 45 ms
// typical and 400 ms worst case, a 256 byte page program 0.7 ms / 3 ms.
#define FLASH_DEV_PAGE      256UL   // NOR program page, datasheet 7.2.x
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
static uint8_t BSP_flashXfer_(uint8_t out);
static void BSP_flashExchange_(uint8_t cmd, uint8_t const *tx, uint32_t txLen,
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
void BSP_flashInit(void) {
    BSP_flashGpioInit_();
    BSP_flashSpiInit_();

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
    BSP_flashExchange_(FLASH_READ_CMD, addr, 3U, dst, len);

    return true;
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
static void BSP_flashExchange_(uint8_t cmd, uint8_t const *tx, uint32_t txLen,
                               uint8_t *rx, uint32_t rxLen) {
    uint32_t i;

    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET);
    (void)BSP_flashXfer_(cmd);

    for (i = 0U; i < txLen; ++i) {
        (void)BSP_flashXfer_(tx[i]);
    }

    for (i = 0U; i < rxLen; ++i) {
        rx[i] = BSP_flashXfer_(0xFFU);
    }

    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);
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
static bool BSP_flashEraseSector_(uint32_t address) {
    uint8_t const addr[3] = {
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)address
    };

    if (!BSP_flashWriteEnable_()) {
        return false;
    }

    BSP_flashExchange_(FLASH_SECTOR_CMD, addr, 3U, (uint8_t *)0, 0U);

    return BSP_flashWaitReady_(FLASH_ERASE_MS);
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
