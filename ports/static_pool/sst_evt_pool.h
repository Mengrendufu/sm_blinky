#ifndef SST_EVT_POOL_H_
#define SST_EVT_POOL_H_

#ifndef SST_EVT_POOL_NUM
#define SST_EVT_POOL_NUM 0U
#endif

#if (SST_EVT_POOL_NUM > 0U)

#include "static_pool.h"

void SST_EvtPool_init(void *sto, PoolCtr poolSize, PoolCtr blockSize);

// What is left in the pools, for the UI's resource page. Both walk every pool
// that was initialised, under the port's own critical section.
unsigned SST_EvtPool_free(void);
unsigned SST_EvtPool_total(void);
unsigned SST_EvtPool_min(void);   // the fewest free blocks ever seen
void *SST_Evt_new(PoolCtr blockSize);
void SST_Evt_gc(void *evt);

#define SST_NEW(evtType_) ((evtType_ *)SST_Evt_new(sizeof(evtType_)))
#define SST_GC(evt_) SST_Evt_gc((void *)(evt_))

#else

#define SST_GC(evt_) ((void)(evt_))

#endif

#endif
