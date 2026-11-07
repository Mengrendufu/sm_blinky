#ifndef NVM_BLINKY_H_
#define NVM_BLINKY_H_

#include "nvm_manager.h"
#include <stdint.h>

// Conventional persisted ID; the instance mounts itself at this slot.
enum { NVM_BLINKY_ID = 2U };

typedef struct {
    uint32_t edges;
} NvmBlinkyData;

// Construct the private singleton and mount it into mgr's slot table.
// Called by the owner's assembly; the mount code stays in this file.
void NvmBlinky_ctor(NvmManager *mgr);

// Accumulate an LED edge into the live block — a post-restore verb;
// writes issued before restore are clobbered by the load commit.
void NvmBlinky_add(uint32_t edges);

// Borrow the live block, not a snapshot. Caller holds NVM_PORT_ENTER/EXIT
// while reading its contents; copy needed values before leaving the section.
NvmBlinkyData const *NvmBlinky_get(void);

#endif /* NVM_BLINKY_H_ */
