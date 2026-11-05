#ifndef NVM_PORT_H_
#define NVM_PORT_H_

// Application port: module + explicit assertion ID, backed by sm_assert.
// Replace this mapping when integrating with another assertion facility.
#include "sm_assert.h"

#define NVM_DEFINE_MODULE(name_) SM_DEFINE_MODULE(name_)

#ifndef SM_DBC_DISABLE
#define NVM_ERROR(id_) SM_onAssert(SM_module_name_, (id_))
#define NVM_ASSERT(id_, cond_) \
    ((cond_) ? (void)0 : NVM_ERROR(id_))
#else
#define NVM_ERROR(id_) ((void)0)
#define NVM_ASSERT(id_, cond_) ((void)0)
#endif

#define NVM_REQUIRE(id_, cond_) NVM_ASSERT(id_, cond_)

// RAM-buffer mutual exclusion: instance ops hold the section only
// while touching object RAM, never across storage IO. This project maps
// to the FreeRTOS critical section; a port may override both macros
// together or leave them empty for a single-body caller.
#ifndef NVM_PORT_ENTER
#include "FreeRTOS.h"
#include "task.h"
#define NVM_PORT_ENTER() taskENTER_CRITICAL()
#define NVM_PORT_EXIT()  taskEXIT_CRITICAL()
#endif

#endif /* NVM_PORT_H_ */
