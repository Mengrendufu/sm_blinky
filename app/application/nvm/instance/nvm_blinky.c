#include "nvm_blinky.h"
#include "nvm_port.h"

typedef struct {
    NvmMeta meta;
    // [0] is the live block; [1] is the backend's IO staging half.
    NvmBlinkyData data[2];
} NvmBlinkyInstance;

static NvmBlinkyInstance instance_;

// The backing must pair exactly: half live block, half IO staging.
_Static_assert(sizeof(instance_.data) == 2U * sizeof(NvmBlinkyData),
               "data must be a pair");

void NvmBlinky_ctor(NvmManager *mgr) {
    NvmMeta_ctor(&instance_.meta, instance_.data,
                  sizeof(instance_.data));
    NvmManager_mountBlock(mgr, NVM_BLINKY_ID, &instance_.meta);
}

void NvmBlinky_add(uint32_t edges) {
    NVM_PORT_ENTER();
    instance_.data[0].edges += edges;
    NVM_PORT_EXIT();
}

NvmBlinkyData const *NvmBlinky_get(void) {
    return &instance_.data[0];
}
