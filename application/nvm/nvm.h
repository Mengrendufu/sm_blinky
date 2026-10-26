//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software; it comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
//============================================================================
#ifndef NVM_H_
#define NVM_H_

#include <stdint.h>
#include <stddef.h>

// Stable IDs: application event ID == item array index == NVS record ID.
// Zero is invalid; preserve assigned numbers across firmware versions.
typedef enum {
    NVM_ID_INVALID = 0U,
    NVM_BOOT_COUNT = 1U,
    NVM_BLINKY_EDGES = 2U,
    NVM_ID_LIMIT
} Nvm_Id;

// Read-only descriptor, indexed by the same ID as the NVS record.
// Registered data has static lifetime; loading always copies size bytes.
typedef struct {
    void *data;
    size_t size;
} Nvm_Meta;

// Returns NULL for an invalid/unregistered ID. Data access still goes through
// module APIs; the descriptor does not grant an unlocked mutable borrow.
Nvm_Meta const *Nvm_meta(Nvm_Id id);

// RAM-only access, safe across application tasks. Restore adds the persisted
// base once, preserving increments already recorded by Blinky.
void Nvm_addBlinky(uint32_t edges);
uint32_t Nvm_blinky(void);
uint32_t Nvm_bootCount(void);

// Flash AO only. load runs once after mount; store snapshots the selected
// item when the event is handled. IO runs outside RAM critical sections.
// Zero means success (including unchanged data), negative means failure.
struct nvs_fs;
int Nvm_load(struct nvs_fs *store);
int Nvm_store(struct nvs_fs *store, Nvm_Id id);

// Last completed store result per item; -EAGAIN means not attempted yet.
// Posting an event does not itself mean that persistence has succeeded.
int Nvm_writeResult(Nvm_Id id);

#endif /* NVM_H_ */
