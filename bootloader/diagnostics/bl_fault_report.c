#include "stm32f1xx_hal.h"
#include "bl_fault_log.h"
#include "bl_link.h"
#include <string.h>

// Reserved fault records 1/2 carry sixteen big-endian words each.
static void Bl_faultBe32_(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void Bl_faultReport_(UART_HandleTypeDef *uart,
                            BSP_FaultRecord const *record,
                            Bl_FaultLogResult result, uint32_t sequence) {
    uint8_t payload[64];
    Bl_faultBe32_(payload, (uint32_t)result);
    Bl_faultBe32_(payload + 4U, sequence);
    for (unsigned i = 0U; i < BSP_FAULT_PAYLOAD_WORDS; ++i) {
        uint32_t word;
        memcpy(&word, (uint8_t const *)record + i * sizeof(word),
               sizeof(word));
        unsigned const offset = i < 14U ? i + 2U : i - 14U;
        Bl_faultBe32_(payload + offset * sizeof(word), word);
        if (i == 13U && !Bl_linkSend(uart, 1U, payload, sizeof(payload))) {
            return;
        }
    }
    (void)Bl_linkSend(uart, 2U, payload, sizeof(payload));
}

bool Bl_faultBoot(UART_HandleTypeDef *uart) {
    extern uint32_t _sfault;
    volatile BSP_FaultRecord *stash =
        (volatile BSP_FaultRecord *)(void *)&_sfault;
    BSP_FaultRecord record;
    BSP_FaultStatus const status = BSP_faultRecordRead_(stash, &record);
    Bl_FaultLogResult result = BL_FAULT_REPLAY;
    uint32_t sequence = 0U;

    if (status == BSP_FAULT_VALID) {
        result = Bl_faultLogSave(&record);
        if (result != BL_FAULT_SAVE_FAILED) {
            // A reset after commit but before this clear is deduplicated.
            BSP_faultRecordClear_(stash);
            __DSB();
            (void)Bl_faultLogLatest(&record, &sequence);
        }
    }
    else {
        if (status == BSP_FAULT_INVALID) {
            BSP_faultRecordClear_(stash);
            uint8_t const unused = 0U;
            if (uart != NULL) {
                (void)Bl_linkSend(uart, 113U, &unused, 0U);
            }
        }
        if (!Bl_faultLogLatest(&record, &sequence)) {
            return false;
        }
    }
    // UART failure never removes persisted history. Failed saves retain RAM.
    if (uart != NULL) {
        Bl_faultReport_(uart, &record, result, sequence);
    }
    return status == BSP_FAULT_VALID;
}
