#include "sst_port.h"

_Static_assert(sizeof(SST_TaskPrio) == sizeof(uint16_t),
               "mixed priority must be 16 bits");
_Static_assert((uint8_t)((24U << 8) | 1U) == 1U,
               "SST priority must use the low byte");
_Static_assert((uint8_t)(((24U << 8) | 1U) >> 8U) == 24U,
               "framework priority must use the high byte");
