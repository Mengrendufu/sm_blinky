#ifndef BL_FAULT_LOG_H_
#define BL_FAULT_LOG_H_

#include "bsp_fault_record.h"
#include <stdbool.h>

typedef enum {
    BL_FAULT_REPLAY = 0,
    BL_FAULT_SAVED = 1,
    BL_FAULT_UNCHANGED = 2,
    BL_FAULT_SAVE_FAILED = 3
} Bl_FaultLogResult;

// Two-page append journal, owned only by bootloader after initialization.
// Equality is over the complete snapshot, against the latest valid record.
// Save never clears the caller's RAM snapshot. Readers never erase logs.
bool Bl_faultLogLatest(BSP_FaultRecord *record, uint32_t *sequence);
Bl_FaultLogResult Bl_faultLogSave(BSP_FaultRecord const *record);

// NULL UART requests persistence without output.
// Returns true for a valid no-init snapshot on this boot, even if saving or
// reporting fails. The caller owns the recovery decision.
// Empty/invalid RAM and Flash history replay return false.
struct __UART_HandleTypeDef;
bool Bl_faultBoot(struct __UART_HandleTypeDef *uart);

#endif // BL_FAULT_LOG_H_
