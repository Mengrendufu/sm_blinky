#ifndef TEST_TIME_EVT_FREERTOS_H_
#define TEST_TIME_EVT_FREERTOS_H_
#include <stddef.h>
void Host_timeEnter(void);
void Host_timeExit(void);
#define taskENTER_CRITICAL() Host_timeEnter()
#define taskEXIT_CRITICAL() Host_timeExit()
#endif
