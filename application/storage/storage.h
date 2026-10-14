#ifndef STORAGE_H_
#define STORAGE_H_

#include <stdint.h>

// The one task that talks to the NVS in the NOR region the flash port owns.
// Everything it learns goes to the tracer; the firmware calls nothing else.
void Storage_start(void);
uint32_t Storage_bootCount(void);

#endif /* STORAGE_H_ */
