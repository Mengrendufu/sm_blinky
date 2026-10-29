//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software; it comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
//============================================================================
#include "nvm.h"
#include "nvs.h"
#include "sm_tracer.h"
#include "FreeRTOS.h"
#include "task.h"
#include <errno.h>
#include <string.h>

// Actual item types are independent; structures and arrays are valid here.
typedef uint32_t Nvm_BootData;
typedef uint32_t Nvm_BlinkyData;
static Nvm_BootData Nvm_boot_;
static Nvm_BlinkyData Nvm_blinky_;
// Application deltas stay separate until the persisted snapshot is loaded.
static uint32_t Nvm_earlyEdges_;
static bool Nvm_loaded_;

// Storage scratch follows registered types, not a fixed scalar/byte limit.
// Register each new type here too; only one item is staged at a time.
typedef union {
    Nvm_BootData boot;
    Nvm_BlinkyData blinky;
} Nvm_IoBuffer;

// Index == application ID == NVS record ID; no offset or key translation.
static Nvm_Meta const Nvm_meta_[NVM_ID_LIMIT] = {
    [NVM_BOOT_COUNT] = {
        .data = &Nvm_boot_, .size = sizeof(Nvm_boot_)
    },
    [NVM_BLINKY_EDGES] = {
        .data = &Nvm_blinky_, .size = sizeof(Nvm_blinky_)
    }
};
static int Nvm_results_[NVM_ID_LIMIT] = {
    [NVM_BOOT_COUNT] = -EAGAIN, [NVM_BLINKY_EDGES] = -EAGAIN
};

Nvm_Meta const *Nvm_meta(Nvm_Id id) {
    if (id <= NVM_ID_INVALID || (unsigned)id >= NVM_ID_LIMIT
        || Nvm_meta_[id].data == NULL) {
        return NULL;
    }
    return &Nvm_meta_[id];
}

void Nvm_addBlinky(uint32_t edges) {
    taskENTER_CRITICAL();
    if (Nvm_loaded_) {
        Nvm_blinky_ += edges;
    }
    else {
        Nvm_earlyEdges_ += edges;
    }
    taskEXIT_CRITICAL();
}

uint32_t Nvm_blinky(void) {
    taskENTER_CRITICAL();
    uint32_t const value = Nvm_blinky_ + Nvm_earlyEdges_;
    taskEXIT_CRITICAL();
    return value;
}

uint32_t Nvm_bootCount(void) {
    taskENTER_CRITICAL();
    uint32_t const value = Nvm_boot_;
    taskEXIT_CRITICAL();
    return value;
}

// == DEBUG: per-item restore failure; protocol 97: UINT16 ID, INT32 error. ==
#define NVM_TRACE_RESTORE_ERROR 97U
static void Nvm_dbgRestoreError_(Nvm_Id id, int error) {
    uint32_t const bits = (uint32_t)(int32_t)error;
    SM_TRACE_BEGIN(NVM_TRACE_RESTORE_ERROR, 6)
    SM_TRACE_PAYLOAD_BE((uint16_t)id >> 8)
    SM_TRACE_PAYLOAD_BE((uint16_t)id)
    SM_TRACE_PAYLOAD_BE(bits >> 24)
    SM_TRACE_PAYLOAD_BE(bits >> 16)
    SM_TRACE_PAYLOAD_BE(bits >> 8)
    SM_TRACE_PAYLOAD_BE(bits)
    SM_TRACE_END(0)
}

// Storage recovery has one policy for all registered data types.
static int Nvm_restore_(struct nvs_fs *store) {
    Nvm_IoBuffer saved;
    int result = 0;

    for (unsigned i = 1U; i < NVM_ID_LIMIT; ++i) {
        Nvm_Meta const *meta = Nvm_meta((Nvm_Id)i);
        if (meta == NULL) {
            continue;
        }
        if (meta->size > sizeof(saved)) {
            Nvm_dbgRestoreError_((Nvm_Id)i, -EOVERFLOW);
            return -EOVERFLOW;
        }
        ssize_t const count = nvs_read(store, (uint16_t)i,
                                       &saved, meta->size);
        if (count != (ssize_t)meta->size) {
            // Missing/failed records leave the application's defaults intact.
            if (count != -ENOENT) {
                int const error = (count < 0) ? (int)count : -EIO;
                Nvm_dbgRestoreError_((Nvm_Id)i, error);
                if (result == 0) {
                    result = error;
                }
            }
            continue;
        }
        taskENTER_CRITICAL();
        memcpy(meta->data, &saved, meta->size);
        taskEXIT_CRITICAL();
    }
    return result;
}

int Nvm_load(struct nvs_fs *store) {
    int const result = Nvm_restore_(store);
    // Application initialization follows storage recovery, outside its loop.
    taskENTER_CRITICAL();
    Nvm_blinky_ += Nvm_earlyEdges_;
    Nvm_earlyEdges_ = 0U;
    Nvm_loaded_ = true;
    ++Nvm_boot_;
    taskEXIT_CRITICAL();
    return result;
}

int Nvm_store(struct nvs_fs *store, Nvm_Id id) {
    Nvm_Meta const *meta = Nvm_meta(id);
    if (meta == NULL) {
        return -EINVAL;
    }
    Nvm_IoBuffer saved;
    if (meta->size > sizeof(saved)) {
        taskENTER_CRITICAL();
        Nvm_results_[id] = -EOVERFLOW;
        taskEXIT_CRITICAL();
        return -EOVERFLOW;
    }
    taskENTER_CRITICAL();
    memcpy(&saved, meta->data, meta->size);
    taskEXIT_CRITICAL();
    ssize_t const count = nvs_write(store, (uint16_t)id, &saved, meta->size);
    int const result = (count == 0 || count == (ssize_t)meta->size)
                       ? 0 : ((count < 0) ? (int)count : -EIO);

    taskENTER_CRITICAL();
    Nvm_results_[id] = result;
    taskEXIT_CRITICAL();
    return result;
}

int Nvm_writeResult(Nvm_Id id) {
    if (Nvm_meta(id) == NULL) {
        return -EINVAL;
    }
    taskENTER_CRITICAL();
    int const result = Nvm_results_[id];
    taskEXIT_CRITICAL();
    return result;
}
