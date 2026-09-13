#ifndef TEST_SST_PORT_H_
#define TEST_SST_PORT_H_

#include <stdint.h>

#define SST_EVT_POOL_NUM 2U
#define SST_MAX_TASK 8U

typedef uint8_t SST_TaskPrio;
typedef int SST_LockKey;

#define SST_PORT_CRIT_STAT
#define SST_PORT_CRIT_ENTRY() ((void)0)
#define SST_PORT_CRIT_EXIT()  ((void)0)

#include "sst_evt_pool.h"

#endif
