// common_c timing synchronization and contract policy for this application.
#ifndef TIME_EVT_PORT_H_
#define TIME_EVT_PORT_H_

#include "FreeRTOS.h"
#include "task.h"
#include "sm_assert.h"

#define TIME_EVT_REQUIRE(condition_) \
    ((condition_) ? (void)0 : SM_onAssert("time_evt", __LINE__))
#define TIME_EVT_CRIT_STAT
#define TIME_EVT_CRIT_ENTRY() taskENTER_CRITICAL()
#define TIME_EVT_CRIT_EXIT() taskEXIT_CRITICAL()

#endif // TIME_EVT_PORT_H_
