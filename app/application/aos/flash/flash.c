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
#include "instance/nvm_boot.h"
#include "instance/nvm_blinky.h"
#include "sm_tracer.h"

#include "FreeRTOS.h"
#include "task.h"

//============================================================================
// Task storage: the thread owns its stack, so the size shows up here and in
// the link map instead of hiding inside an RTOS heap.
#define FLASH_STACK_WORDS 256U // 256 words = 1 KB; see Sys watermark

static StaticTask_t Flash_tcb_;
static StackType_t Flash_stack_[FLASH_STACK_WORDS];
static Sm_Flash Flash_inst_;
SST_Task * const AO_Flash = &Flash_inst_.super;

//============================================================================

//============================================================================
void Flash_ctor(void) {
    SST_Task_ctor(AO_Flash, &Sm_Flash_init, &Sm_Flash_dispatch);
    AO_Flash->tcb = &Flash_tcb_;
    AO_Flash->stack = Flash_stack_;
    AO_Flash->stackSizeWords = FLASH_STACK_WORDS;
    NvmManager_ctor(&Flash_inst_.nvm);
    NvmBoot_ctor(&Flash_inst_.nvm);
    NvmBlinky_ctor(&Flash_inst_.nvm);
}
//............................................................................
// The manager restores on this AO's task, after asset loading.
#define FLASH_NVS_MOUNT_ATTEMPTS 2U
#define FLASH_TRACE_NVS_MOUNT_ERROR 8U
#define FLASH_TRACE_NVS_UNAVAILABLE 9U
#define FLASH_TRACE_NVM_STORE_ERROR 11U

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

// Record 91 is retired. Each boot value now retains all 32 bits.
#define FLASH_TRACE_NVS_INIT_MS 13U
#define FLASH_TRACE_NVS_RESTORE_RESULT 14U
#define FLASH_TRACE_NVM_BOOT_COUNT 15U

static void Flash_dbgNvsValue_(uint8_t record, uint32_t value) {
    SM_TRACE_BEGIN(record, 4)
        SM_TRACE_PAYLOAD_BE(value >> 24)
        SM_TRACE_PAYLOAD_BE(value >> 16)
        SM_TRACE_PAYLOAD_BE(value >> 8)
        SM_TRACE_PAYLOAD_BE(value)
    SM_TRACE_END(0)
}

static void Flash_nvsBoot_(void) {
    NvmManager * const mgr = &Flash_inst_.nvm;
    struct nvs_fs const * const fs = mgr->fs;
    uint32_t const t0 = HAL_GetTick();
    int result = 0;
    for (
        unsigned attempt = 1U;
        attempt <= FLASH_NVS_MOUNT_ATTEMPTS;
        ++attempt
    ) {
        result = NvmManager_init(mgr);
        // A restore error must not cause a second restore of live data.
        if (fs->ready) {
            break;
        }
        Flash_dbgNvsError_(FLASH_TRACE_NVS_MOUNT_ERROR,
                           (uint16_t)attempt, result);
    }
    if (!fs->ready) {
        // Leave NVS unready; raw Flash requests remain independent.
        Flash_dbgNvsError_(FLASH_TRACE_NVS_UNAVAILABLE,
                           FLASH_NVS_MOUNT_ATTEMPTS, result);
    }
    // Count this boot even when storage is unavailable; RAM stays usable.
    NvmBoot_inc();
    uint32_t const initMs = HAL_GetTick() - t0;
    Flash_dbgNvsValue_(FLASH_TRACE_NVS_INIT_MS, initMs);
    Flash_dbgNvsValue_(FLASH_TRACE_NVS_RESTORE_RESULT,
                       (uint32_t)(int32_t)result);
    Flash_dbgNvsValue_(FLASH_TRACE_NVM_BOOT_COUNT, NvmBoot_get()->count);
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

void Flash_writeNvm(uint16_t id) {
    // Enqueue success is not save success; IO failures are traced here.
    int const result = NvmManager_store(&Flash_inst_.nvm, id);
    if (result != 0) {
        Flash_dbgNvsError_(FLASH_TRACE_NVM_STORE_ERROR,
                           (uint16_t)id, result);
    }
}

void Flash_completeRequest(Flash_Req * const req) {
    Flash_reqFinish(req);
}
