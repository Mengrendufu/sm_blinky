#include "nvm_boot.h"
#include "../nvm_port.h"

typedef struct {
    NvmMeta meta;
    // [0] is the live block; [1] is the backend's IO staging half.
    NvmBootData data[2];
} NvmBootInstance;

static NvmBootInstance instance_;

// The backing must pair exactly: half live block, half IO staging.
_Static_assert(sizeof(instance_.data) == 2U * sizeof(NvmBootData),
               "data must be a pair");

void NvmBoot_ctor(NvmManager *mgr) {
    NvmMeta_ctor(&instance_.meta, instance_.data,
                  sizeof(instance_.data));
    NvmManager_mountBlock(mgr, NVM_BOOT_ID, &instance_.meta);
}

void NvmBoot_inc(void) {
    NVM_PORT_ENTER();
    ++instance_.data[0].count;
    NVM_PORT_EXIT();
}

NvmBootData const *NvmBoot_get(void) {
    return &instance_.data[0];
}
