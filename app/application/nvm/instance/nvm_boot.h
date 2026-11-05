#ifndef NVM_BOOT_H_
#define NVM_BOOT_H_

#include "../nvm_manager.h"
#include <stdint.h>

// Conventional persisted ID; the instance mounts itself at this slot.
enum { NVM_BOOT_ID = 1U };

typedef struct {
    uint32_t count;
} NvmBootData;

// Construct the private singleton and mount it into mgr's slot table.
// Called by the owner's assembly; the mount code stays in this file.
void NvmBoot_ctor(NvmManager *mgr);

// Post-restore business step: the loaded count gains this boot.
void NvmBoot_inc(void);

// Borrow the live block, not a snapshot. Caller holds NVM_PORT_ENTER/EXIT
// while reading its contents; copy needed values before leaving the section.
NvmBootData const *NvmBoot_get(void);

#endif /* NVM_BOOT_H_ */
