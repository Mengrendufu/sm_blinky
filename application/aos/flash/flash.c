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

#include "FreeRTOS.h"
#include "task.h"

//============================================================================
// Task storage: the thread owns its stack, so the size shows up here and in
// the link map instead of hiding inside an RTOS heap.
#define FLASH_STACK_WORDS 256U // 256 words = 1 KB; settle it from [MEM]

// Completed requests between [MEM][STACK][FLASH] reports. The first request
// reports as well, so a boot that issues a single one still says how much
// stack the path took.
#define FLASH_REPORT_EVERY 8U

static StaticTask_t Flash_tcb_;
static StackType_t Flash_stack_[FLASH_STACK_WORDS];
static Sm_Flash Flash_inst_;
SST_Task * const AO_Flash = &Flash_inst_.super;
static uint32_t Flash_done_;

//============================================================================
static void Flash_report_(void);

//============================================================================
void Flash_ctor(void) {
    SST_Task_ctor(AO_Flash, &Sm_Flash_init, &Sm_Flash_dispatch);
    AO_Flash->tcb = &Flash_tcb_;
    AO_Flash->stack = Flash_stack_;
    AO_Flash->stackSizeWords = FLASH_STACK_WORDS;
}
//............................................................................
// NVS belongs to this AO. Restore runs on its task after asset loading.
static struct nvs_fs Flash_nvs_;
#define FLASH_NVS_MOUNT_ATTEMPTS 2U
#define FLASH_TRACE_NVS_MOUNT_ERROR 95U
#define FLASH_TRACE_NVS_UNAVAILABLE 96U
#define FLASH_TRACE_NVM_STORE_ERROR 98U

// == DEBUG: failure records; signed error is encoded as four BE bytes. ==
static void Flash_dbgNvsError_(uint8_t record, uint16_t detail, int error) {
    uint32_t const bits = (uint32_t)(int32_t)error;
    SM_TRACE_BEGIN(record, 6)
    SM_TRACE_PAYLOAD_BE(detail >> 8)
    SM_TRACE_PAYLOAD_BE(detail)
    SM_TRACE_PAYLOAD_BE(bits >> 24)
    SM_TRACE_PAYLOAD_BE(bits >> 16)
    SM_TRACE_PAYLOAD_BE(bits >> 8)
    SM_TRACE_PAYLOAD_BE(bits)
    SM_TRACE_END(0)
}

static uint8_t Flash_dbgNvsMs_(uint32_t ms) {
    return (ms > 255U) ? 255U : (uint8_t)ms;
}

static void Flash_nvsBoot_(void) {
    uint32_t t0;
    uint32_t mountMs;
    uint32_t readMs;

    Flash_nvs_.offset = 0U;
    Flash_nvs_.sector_size = 4096U;   // the W25Q128's erasable sector
    Flash_nvs_.sector_count = NOR_NVS_SECTORS;   // 256 KB: sixteen times
                                                 // the sectors, sixteen
                                                 // times the erase budget
    Flash_nvs_.flash_device = nvs_port_device();

    t0 = HAL_GetTick();
    int mounted = 0;
    for (unsigned attempt = 1U; attempt <= FLASH_NVS_MOUNT_ATTEMPTS;
         ++attempt) {
        mounted = nvs_mount(&Flash_nvs_);
        if (mounted == 0) {
            break;
        }
        Flash_dbgNvsError_(FLASH_TRACE_NVS_MOUNT_ERROR,
                           (uint16_t)attempt, mounted);
    }
    if (mounted != 0) {
        // Leave NVS unready; raw Flash requests remain independent.
        Flash_dbgNvsError_(FLASH_TRACE_NVS_UNAVAILABLE,
                           FLASH_NVS_MOUNT_ATTEMPTS, mounted);
        return;
    }
    mountMs = HAL_GetTick() - t0;

    t0 = HAL_GetTick();
    int const restored = Nvm_load(&Flash_nvs_);
    readMs = HAL_GetTick() - t0;
    uint32_t const boots = Nvm_bootCount();

    SM_TRACE_BEGIN(91U, 6)
    SM_TRACE_PAYLOAD_BE(Flash_dbgNvsMs_(mountMs))
    SM_TRACE_PAYLOAD_BE((unsigned char)(restored == 0))
    SM_TRACE_PAYLOAD_BE(Flash_dbgNvsMs_(readMs))
    SM_TRACE_PAYLOAD_BE((unsigned char)(boots & 0xFFU))
    SM_TRACE_PAYLOAD_BE((unsigned char)((boots >> 8) & 0xFFU))
    SM_TRACE_PAYLOAD_BE(3U)   // tag 3: mount/restore, no application write
    SM_TRACE_END(0)
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

void Flash_writeNvm(Nvm_Id id) {
    // NVM records the result per item; enqueue success is not save success.
    int const result = Nvm_store(&Flash_nvs_, id);
    if (result != 0) {
        Flash_dbgNvsError_(FLASH_TRACE_NVM_STORE_ERROR,
                           (uint16_t)id, result);
    }
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
