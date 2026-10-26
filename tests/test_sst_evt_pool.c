#include <assert.h>
#include <stdint.h>

#include "sst.h"

typedef struct {
    SST_Evt super;
    uint32_t payload[4];
} LargeEvt;

void DBC_fault_handler(char const *module, int label)
{
    (void)module;
    (void)label;
    assert(0);
}

int main(void)
{
    static STATIC_POOL_ELEM_TYPE(SST_Evt) smallSto[2];
    static STATIC_POOL_ELEM_TYPE(LargeEvt) largeSto[2];

    SST_EvtPool_init(smallSto, sizeof(smallSto), sizeof(SST_Evt));
    SST_EvtPool_init(largeSto, sizeof(largeSto), sizeof(LargeEvt));

    SST_Evt *small = SST_NEW(SST_Evt);
    LargeEvt *large = SST_NEW(LargeEvt);

    assert(small->poolId == 1U);
    assert(large->super.poolId == 2U);
    assert(small->refCtr == 0U);
    SST_EvtPoolLevel level = SST_EvtPool_level(1U);
    assert(level.min == 1U && level.total == 2U);
    SST_Evt *second = SST_NEW(SST_Evt);
    level = SST_EvtPool_level(1U);
    assert(level.min == 0U && level.total == 2U);
    level = SST_EvtPool_level(2U);
    assert(level.min == 1U && level.total == 2U);
    SST_GC(second);
    // Returning blocks must not raise the historical minimum.
    assert(SST_EvtPool_level(1U).min == 0U);

    small->refCtr = 2U;
    SST_GC(small);
    assert(small->refCtr == 1U);

    SST_GC(small);
    assert(SST_NEW(SST_Evt) == small);

    SST_Evt staticEvt = {.sig = 1U, .poolId = 0U, .refCtr = 0U};
    SST_GC(&staticEvt);
    assert(staticEvt.poolId == 0U);

    SST_GC(&large->super);
    return 0;
}
