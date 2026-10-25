//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
//============================================================================
//=== Flash AO -- one event at a time, with yielding waits inside dispatch

#include "bsp.h"
#include "flash.h"
#include "application.h"
#include "bsp_flash.h"
#include "sm_flash.h"
#include "flash_req_priv.h"
#include "flash_priv.h"
#include "nvs.h"
#include "nvm.h"
#include "nor_partitions.h"   // where the log lives and how many sectors
#include "sm_tracer.h"
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

//============================================================================
// Task storage: the thread owns its stack, so the size shows up here and in
// the link map instead of hiding inside an RTOS heap.
#define FLASH_STACK_WORDS 256U // 256 words = 1 KB; settle it from [MEM]

// One-shot checkpoint: rearmed only after its handler finishes, so long
// transfers cannot accumulate periodic checkpoint events.
#define FLASH_NVM_MS      10000U

// NVM_Blinky lives under this key in the store.
#define NVM_BLINKY_KEY    2U

// Completed requests between [MEM][STACK][FLASH] reports. The first request
// reports as well, so a boot that issues a single one still says how much
// stack the path took.
#define FLASH_REPORT_EVERY 8U

static StaticTask_t Flash_tcb_;
static StackType_t Flash_stack_[FLASH_STACK_WORDS];
static Sm_Flash Flash_inst_;
SST_Task * const AO_Flash = &Flash_inst_.super;
static SST_TimeEvt Flash_checkpoint_;
static uint32_t Flash_done_;

//============================================================================
static void Flash_report_(void);

//============================================================================
void Flash_ctor(void) {
    SST_Task_ctor(AO_Flash, &Sm_Flash_init, &Sm_Flash_dispatch);
    AO_Flash->tcb = &Flash_tcb_;
    AO_Flash->stack = Flash_stack_;
    AO_Flash->stackSizeWords = FLASH_STACK_WORDS;
    SST_TimeEvt_ctor(&Flash_checkpoint_, FLASH_CHECKPOINT_SIG, AO_Flash);
}
//............................................................................
// NVS belongs to this AO. Restore runs on its task after asset loading.
static struct nvs_fs Flash_nvs_;
static uint32_t Flash_boot_;

static uint32_t Flash_dbgNvsCycles_(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    return DWT->CYCCNT;
}

static uint8_t Flash_dbgNvsMs_(uint32_t cycles) {
    uint32_t const ms = cycles / (SystemCoreClock / 1000U);

    return (ms > 255U) ? 255U : (uint8_t)ms;
}

static void Flash_nvsBoot_(void) {
    uint32_t before = 0U;
    uint32_t t0;
    uint32_t mountCycles;
    uint32_t readCycles;
    uint32_t writeCycles;
    bool have;

    Flash_nvs_.offset = 0U;
    Flash_nvs_.sector_size = 4096U;   // the W25Q128's erasable sector
    Flash_nvs_.sector_count = NOR_NVS_SECTORS;   // 256 KB: sixteen times
                                                 // the sectors, sixteen
                                                 // times the erase budget
    Flash_nvs_.flash_device = nvs_port_device();

    t0 = Flash_dbgNvsCycles_();
    if (nvs_mount(&Flash_nvs_) != 0) {
        (void)nvs_clear(&Flash_nvs_);
        if (nvs_mount(&Flash_nvs_) != 0) {
            return;   // nothing to report this boot
        }
    }
    mountCycles = Flash_dbgNvsCycles_() - t0;

    t0 = Flash_dbgNvsCycles_();
    have = (nvs_read(&Flash_nvs_, 1U, &before, sizeof(before))
            == (ssize_t)sizeof(before));
    readCycles = Flash_dbgNvsCycles_() - t0;

    Flash_boot_ = have ? (before + 1U) : 1U;

    t0 = Flash_dbgNvsCycles_();
    (void)nvs_write(&Flash_nvs_, 1U, &Flash_boot_, sizeof(Flash_boot_));
    writeCycles = Flash_dbgNvsCycles_() - t0;

    SM_TRACE_BEGIN(91U, 6)
    SM_TRACE_PAYLOAD_BE(Flash_dbgNvsMs_(mountCycles))
    SM_TRACE_PAYLOAD_BE(Flash_dbgNvsMs_(writeCycles))
    SM_TRACE_PAYLOAD_BE(Flash_dbgNvsMs_(readCycles))
    SM_TRACE_PAYLOAD_BE((unsigned char)(Flash_boot_ & 0xFFU))
    SM_TRACE_PAYLOAD_BE((unsigned char)((Flash_boot_ >> 8) & 0xFFU))
    SM_TRACE_PAYLOAD_BE(2U)   // tag 2: the NVS lives in the flash task now
    SM_TRACE_END(0)

    // NVM_Blinky: what the last run left behind goes into the snapshot the UI
    // reads. A first power-up finds nothing, and the snapshot starts at zero.
    {
        uint32_t blinky = 0U;

        if (nvs_read(&Flash_nvs_, NVM_BLINKY_KEY, &blinky, sizeof(blinky))
            == (ssize_t)sizeof(blinky)) {
            // Preserve deltas already published by the running UI.
            Nvm_addBlinky(blinky);
        }
    }
}

// The KV side of the AO: the same store the boot code mounted, reached by the
// two requests the HSM dispatches. Still one execution body, still one bus.
bool Flash_kvWrite(uint16_t id, void const *src, uint32_t len) {
    return (nvs_write(&Flash_nvs_, id, src, len) == (ssize_t)len);
}

bool Flash_kvRead(uint16_t id, void *dst, uint32_t len) {
    return (nvs_read(&Flash_nvs_, id, dst, len) == (ssize_t)len);
}

//============================================================================
// RUN WAIT OPERATIONS -- the AO task; slot 0 is SST, slot 1 is hardware/RPC.
#define FLASH_DMA_DONE_INDEX 1U

static bool Flash_waitIsCaller_(void *context) {
    return (context != (void *)0)
           && (xPortIsInsideInterrupt() == pdFALSE)
           && (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
           && (xTaskGetCurrentTaskHandle() == (TaskHandle_t)context);
}

static bool Flash_waitDone_(void *context, uint32_t timeoutMs) {
    (void)context;
    return ulTaskNotifyTakeIndexed(FLASH_DMA_DONE_INDEX, pdTRUE,
                                   pdMS_TO_TICKS(timeoutMs)) != 0U;
}

static void Flash_notifyDone_(void *context) {
    TaskHandle_t const waiter = (TaskHandle_t)context;

    if (xPortIsInsideInterrupt() != pdFALSE) {
        BaseType_t woken = pdFALSE;

        (void)vTaskNotifyGiveIndexedFromISR(waiter, FLASH_DMA_DONE_INDEX,
                                           &woken);
        portYIELD_FROM_ISR(woken);
    }
    else {
        (void)xTaskNotifyGiveIndexed(waiter, FLASH_DMA_DONE_INDEX);
    }
}

static void Flash_sleepMs_(void *context, uint32_t delayMs) {
    (void)context;
    vTaskDelay(pdMS_TO_TICKS(delayMs));
}

static void Flash_yield_(void *context) {
    (void)context;
    taskYIELD();
}

static uint32_t Flash_now_(void *context) {
    (void)context;
    return HAL_GetTick();
}

static uint32_t Flash_elapsedMs_(void *context, uint32_t started) {
    (void)context;
    return HAL_GetTick() - started;
}

static BSP_flashWaitOperations const Flash_runWaitOperations_ = {
    .isCaller = &Flash_waitIsCaller_,
    .waitDone = &Flash_waitDone_,
    .notifyDone = &Flash_notifyDone_,
    .sleepMs = &Flash_sleepMs_,
    .yield = &Flash_yield_,
    .now = &Flash_now_,
    .elapsedMs = &Flash_elapsedMs_,
};

//============================================================================
// LIFECYCLE -- all device operations run on the owning AO task.
void Flash_bindRuntime(void) {
    configASSERT(xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED);
    configASSERT(xTaskGetCurrentTaskHandle() == AO_Flash->thread);
    Application_dbgBootReport();
    BSP_flashBind(BSP_flashDevice(), &BSP_flashDmaOperations,
                   &Flash_runWaitOperations_, (void *)AO_Flash->thread);
}

void Flash_initStorage(void) {
    Flash_nvsBoot_();
    Flash_reqBind(AO_Flash);
}

void Flash_armCheckpoint(void) {
    SST_TimeEvt_arm(&Flash_checkpoint_,
                    FLASH_NVM_MS * BSP_TICKS_PER_SEC / 1000U, 0U);
}

void Flash_checkpoint(void) {
    uint32_t const v = Nvm_blinky();

    (void)nvs_write(&Flash_nvs_, NVM_BLINKY_KEY, &v, sizeof(v));
}

void Flash_completeRequest(Flash_Req * const req) {
    Flash_reqFinish(req);
    ++Flash_done_;
    Flash_report_();
}
//............................................................................
static void Flash_report_(void) {
    if ((Flash_done_ == 1U) || ((Flash_done_ % FLASH_REPORT_EVERY) == 0U)) {
        BSP_dbgMemReport(BSP_TRACE_STACK_FLASH);
    }
}
