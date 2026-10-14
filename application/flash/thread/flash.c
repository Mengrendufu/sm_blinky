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
//=== Flash thread -- wait, drain, dispatch

#include "bsp.h"
#include "flash.h"
#include "sm_flash.h"
#include "flash_req_priv.h"
#include "nvs.h"
#include "sm_tracer.h"
#include <string.h>

/* TODO(probe): remove with the flood below. The port counts erases for us. */
#include "FreeRTOS.h"
#include "task.h"

//============================================================================
// Task storage: the thread owns its stack, so the size shows up here and in
// the link map instead of hiding inside an RTOS heap.
#define FLASH_STACK_WORDS 256U // 256 words = 1 KB; settle it from [MEM]

// Same level as the UI thread (23) and under the Blinky AO (24). It must not
// sit below its callers: a notification wait has no priority inheritance, so
// a middle-priority task in between would stretch every caller's block.
#define FLASH_PRIORITY    23U

// Completed requests between [MEM][STACK][FLASH] reports. The first request
// reports as well, so a boot that issues a single one still says how much
// stack the path took.
#define FLASH_REPORT_EVERY 8U

static StaticTask_t Flash_tcb_;
static StackType_t Flash_stack_[FLASH_STACK_WORDS];
static Sm_Flash Flash_sm_;
static TaskHandle_t Flash_handle_;
static uint32_t Flash_done_;

//============================================================================
static void Flash_thread_(void *arg);
static void Flash_drain_(void);
static void Flash_report_(void);

//============================================================================
void Flash_start(void) {
    Sm_Flash_init(&Flash_sm_);

    Flash_handle_ = xTaskCreateStatic(Flash_thread_, "flash",
                                      FLASH_STACK_WORDS, (void *)0,
                                      FLASH_PRIORITY, Flash_stack_,
                                      &Flash_tcb_);
    configASSERT(Flash_handle_ != (TaskHandle_t)0);

    Flash_reqBind(Flash_handle_);
}
//............................................................................
// No poll timeout: this thread has no periodic work at all, so it sleeps
// until a request arrives. That is the difference from an AO, which would
// have a beat to keep up with.
// The NVS is a capability of this AO, not a second user of the flash: it
// lives in this thread, and nvs_port_flash.c talks to the bus directly. On
// the way up it mounts, reads the boot counter, writes it back one higher and
// traces what each step cost (record 91: mount ms, write ms, read ms, boot
// count low, boot count high, tag).
//
// Four sectors on purpose: the mount scans the ATE area of every sector in
// 8-byte reads, so 16 sectors cost about 111 ms on the glass and four cost a
// quarter of that, which is plenty of room for a key-value store this size.
static struct nvs_fs Flash_nvs_;
static uint32_t Flash_boot_;

static uint32_t Flash_nvs_us_(void) {
    return ((uint32_t)HAL_GetTick() * 1000U) + (uint32_t)TIM1->CNT;
}

static uint8_t Flash_nvs_ms_(uint32_t us) {
    uint32_t const ms = us / 1000U;

    return (ms > 255U) ? 255U : (uint8_t)ms;
}

static void Flash_nvsBoot_(void) {
    uint32_t before = 0U;
    uint32_t t0;
    uint32_t mountUs;
    uint32_t readUs;
    uint32_t writeUs;
    bool have;

    Flash_nvs_.offset = 0U;
    Flash_nvs_.sector_size = 4096U;   // the W25Q128's erasable sector
    Flash_nvs_.sector_count = 4U;
    Flash_nvs_.flash_device = nvs_port_device();

    t0 = Flash_nvs_us_();
    if (nvs_mount(&Flash_nvs_) != 0) {
        (void)nvs_clear(&Flash_nvs_);
        if (nvs_mount(&Flash_nvs_) != 0) {
            return;   // nothing to report this boot
        }
    }
    mountUs = Flash_nvs_us_() - t0;

    t0 = Flash_nvs_us_();
    have = (nvs_read(&Flash_nvs_, 1U, &before, sizeof(before))
            == (ssize_t)sizeof(before));
    readUs = Flash_nvs_us_() - t0;

    Flash_boot_ = have ? (before + 1U) : 1U;

    t0 = Flash_nvs_us_();
    (void)nvs_write(&Flash_nvs_, 1U, &Flash_boot_, sizeof(Flash_boot_));
    writeUs = Flash_nvs_us_() - t0;

    SM_TRACE_BEGIN(91U, 6)
    SM_TRACE_PAYLOAD_BE(Flash_nvs_ms_(mountUs))
    SM_TRACE_PAYLOAD_BE(Flash_nvs_ms_(writeUs))
    SM_TRACE_PAYLOAD_BE(Flash_nvs_ms_(readUs))
    SM_TRACE_PAYLOAD_BE((unsigned char)(Flash_boot_ & 0xFFU))
    SM_TRACE_PAYLOAD_BE((unsigned char)((Flash_boot_ >> 8) & 0xFFU))
    SM_TRACE_PAYLOAD_BE(2U)   // tag 2: the NVS lives in the flash task now
    SM_TRACE_END(0)
}

// The KV side of the AO: the same store the boot code mounted, reached by the
// two requests the HSM dispatches. Still one execution body, still one bus.
bool Flash_kvWrite(uint16_t id, void const *src, uint32_t len) {
    return (nvs_write(&Flash_nvs_, id, src, len) == (ssize_t)len);
}

bool Flash_kvRead(uint16_t id, void *dst, uint32_t len) {
    return (nvs_read(&Flash_nvs_, id, dst, len) == (ssize_t)len);
}

static void Flash_thread_(void *arg) {
    (void)arg;

    Flash_nvsBoot_();

    for (;;) {
        // WAIT: nothing pending, so the thread parks here and the CPU goes
        // elsewhere until a producer writes slot 0.
        (void)ulTaskNotifyTakeIndexed(FLASH_REQ_INDEX, pdTRUE, portMAX_DELAY);
        Flash_drain_();
    }
}
//............................................................................
static void Flash_drain_(void) {
    Flash_Req *req;

    while (Flash_reqPop(&req)) {
        Flash_Evt const evt = { FLASH_REQ_SIG, req };

        Sm_Flash_dispatch(&Flash_sm_, &evt);
        Flash_reqFinish(req);

        ++Flash_done_;
        Flash_report_();
    }
}
//............................................................................
static void Flash_report_(void) {
    if ((Flash_done_ == 1U) || ((Flash_done_ % FLASH_REPORT_EVERY) == 0U)) {
        BSP_dbgMemReport(BSP_TRACE_STACK_FLASH);
    }
}
