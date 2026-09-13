#include "sst.h"

#if (SST_EVT_POOL_NUM > 0U)

#include "dbc_assert.h"

DBC_MODULE_NAME("sst_evt_pool")

static uint8_t l_evtPoolsNum;
static StaticPool l_evtPools[SST_EVT_POOL_NUM];

void SST_EvtPool_init(void * const sto,
                      PoolCtr const poolSize,
                      PoolCtr const blockSize)
{
    DBC_REQUIRE(100, l_evtPoolsNum < SST_EVT_POOL_NUM);
    DBC_REQUIRE(101, sto != (void *)0);
    DBC_REQUIRE(102, blockSize >= sizeof(SST_Evt));

    StaticPool_init(&l_evtPools[l_evtPoolsNum], sto, poolSize, blockSize);
    ++l_evtPoolsNum;
}

void *SST_Evt_new(PoolCtr const blockSize)
{
    DBC_REQUIRE(200, blockSize >= sizeof(SST_Evt));

    SST_PORT_CRIT_ENTRY();

    uint8_t poolNum = 0U;
    while ((poolNum < l_evtPoolsNum)
           && (l_evtPools[poolNum].blockSize < blockSize)) {
        ++poolNum;
    }

    DBC_ENSURE(201, poolNum < l_evtPoolsNum);

    SST_Evt * const evt = (SST_Evt *)StaticPool_get(&l_evtPools[poolNum]);
    DBC_ENSURE(202, evt != (SST_Evt *)0);

    evt->poolId = poolNum + 1U;
    evt->refCtr = 0U;

    SST_PORT_CRIT_EXIT();
    return evt;
}

void SST_Evt_gc(void * const evt)
{
    SST_Evt * const e = (SST_Evt *)evt;

    DBC_REQUIRE(300, e != (SST_Evt *)0);

    SST_PORT_CRIT_ENTRY();

    if (e->poolId != 0U) {
        uint8_t const poolNum = e->poolId - 1U;
        DBC_INVARIANT(301, poolNum < l_evtPoolsNum);

        if (e->refCtr > 1U) {
            --e->refCtr;
        } else {
            StaticPool_put(&l_evtPools[poolNum], evt);
        }
    }

    SST_PORT_CRIT_EXIT();
}

#endif
