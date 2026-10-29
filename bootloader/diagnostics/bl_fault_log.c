#include "stm32f1xx_hal.h"
#include "bl_fault_log.h"
#include "project_layout.h"
#include <string.h>

// Commit also identifies the journal format. Payload is written/read back
// before this final marker; torn entries are occupied but never valid.
#define BL_LOG_COMMIT 0x464C4731U

typedef struct {
    uint32_t sequence;
    BSP_FaultRecord record;
    uint32_t checksum;
    uint32_t commit;
} Bl_LogEntry;

#define BL_LOG_SLOTS (PROJECT_LOG_PAGE_BYTES / sizeof(Bl_LogEntry))
_Static_assert(sizeof(Bl_LogEntry) == 136U, "journal entry layout");
_Static_assert(FLASH_PAGE_SIZE == PROJECT_LOG_PAGE_BYTES, "page geometry");
_Static_assert(PROJECT_LOG_PAGES == 2, "two-page rotation");
_Static_assert(PROJECT_LOG_BASE % FLASH_PAGE_SIZE == 0, "page alignment");

static uint32_t Bl_logAddress_(unsigned page, unsigned slot) {
    return PROJECT_LOG_BASE + page * PROJECT_LOG_PAGE_BYTES
           + slot * sizeof(Bl_LogEntry);
}

static uint32_t Bl_logChecksum_(Bl_LogEntry const *entry) {
    uint8_t const *bytes = (uint8_t const *)entry;
    uint32_t hash = BL_LOG_COMMIT;

    for (unsigned i = 0U; i < offsetof(Bl_LogEntry, checksum); ++i) {
        hash = (hash ^ bytes[i]) * 16777619U;
    }
    return hash;
}

static bool Bl_logRead_(unsigned page, unsigned slot, Bl_LogEntry *entry) {
    *entry = *(volatile Bl_LogEntry const *)(uintptr_t)
        Bl_logAddress_(page, slot);
    return entry->commit == BL_LOG_COMMIT
           && BSP_faultRecordStatus_(&entry->record) == BSP_FAULT_VALID
           && entry->checksum == Bl_logChecksum_(entry);
}

static bool Bl_logLatest_(Bl_LogEntry *latest, unsigned *page,
                          unsigned *slot) {
    bool found = false;

    for (unsigned p = 0U; p < PROJECT_LOG_PAGES; ++p) {
        for (unsigned s = 0U; s < BL_LOG_SLOTS; ++s) {
            Bl_LogEntry entry;
            if (!Bl_logRead_(p, s, &entry)) {
                continue;
            }
            uint32_t const delta = entry.sequence - latest->sequence;
            if (!found || (delta != 0U && delta < 0x80000000U)) {
                *latest = entry;
                *page = p;
                *slot = s;
                found = true;
            }
        }
    }
    return found;
}

bool Bl_faultLogLatest(BSP_FaultRecord *record, uint32_t *sequence) {
    Bl_LogEntry latest = {0};
    unsigned page, slot;

    if (!Bl_logLatest_(&latest, &page, &slot)) {
        return false;
    }
    *record = latest.record;
    *sequence = latest.sequence;
    return true;
}

static bool Bl_logBlank_(unsigned page, unsigned slot) {
    volatile uint32_t const *words = (volatile uint32_t const *)(uintptr_t)
        Bl_logAddress_(page, slot);
    for (unsigned i = 0U; i < sizeof(Bl_LogEntry) / sizeof(*words); ++i) {
        if (words[i] != 0xFFFFFFFFU) {
            return false;
        }
    }
    return true;
}

static bool Bl_logFree_(unsigned page, unsigned start, unsigned *slot) {
    for (unsigned s = start; s < BL_LOG_SLOTS; ++s) {
        if (Bl_logBlank_(page, s)) {
            *slot = s;
            return true;
        }
    }
    return false;
}

static bool Bl_logProgram_(uint32_t address, void const *data, size_t len) {
    uint8_t const *bytes = data;

    for (size_t i = 0U; i < len; i += 2U) {
        uint16_t const value = bytes[i] | ((uint16_t)bytes[i + 1U] << 8);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                              address + i, value) != HAL_OK) {
            return false;
        }
        if (*(volatile uint16_t const *)(uintptr_t)(address + i) != value) {
            return false;
        }
    }
    return true;
}

Bl_FaultLogResult Bl_faultLogSave(BSP_FaultRecord const *record) {
    if (BSP_faultRecordStatus_(record) != BSP_FAULT_VALID) {
        return BL_FAULT_SAVE_FAILED;
    }
    Bl_LogEntry latest = {0};
    unsigned page = 0U, slot = 0U;
    bool const found = Bl_logLatest_(&latest, &page, &slot);
    if (found && memcmp(&latest.record, record, sizeof(*record)) == 0) {
        return BL_FAULT_UNCHANGED;
    }

    // Fill the current page, then the other. Erase only the other page,
    // so the latest committed snapshot survives an interrupted rollover.
    bool erase = false;
    if (!Bl_logFree_(page, found ? slot + 1U : 0U, &slot)) {
        page ^= 1U;
        if (!Bl_logFree_(page, 0U, &slot)) {
            erase = true;
            slot = 0U;
        }
    }
    if (__HAL_FLASH_GET_FLAG(FLASH_FLAG_BSY)
        || HAL_FLASH_Unlock() != HAL_OK) {
        return BL_FAULT_SAVE_FAILED;
    }
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR
                          | FLASH_FLAG_WRPERR);
    bool ok = true;
    if (erase) {
        FLASH_EraseInitTypeDef request = {
            .TypeErase = FLASH_TYPEERASE_PAGES,
            .PageAddress = PROJECT_LOG_BASE + page * PROJECT_LOG_PAGE_BYTES,
            .NbPages = 1U
        };
        uint32_t error;
        ok = HAL_FLASHEx_Erase(&request, &error) == HAL_OK;
    }
    Bl_LogEntry entry = {
        .sequence = found ? latest.sequence + 1U : 1U,
        .record = *record,
        .commit = BL_LOG_COMMIT
    };
    entry.checksum = Bl_logChecksum_(&entry);
    uint32_t const address = Bl_logAddress_(page, slot);
    ok = ok && Bl_logProgram_(address, &entry, offsetof(Bl_LogEntry, commit));
    if (ok) {
        __DSB();
        ok = Bl_logProgram_(address + offsetof(Bl_LogEntry, commit),
                            &entry.commit, sizeof(entry.commit));
    }
    (void)HAL_FLASH_Lock();
    Bl_LogEntry verify;
    return ok && Bl_logRead_(page, slot, &verify)
           && memcmp(&entry, &verify, sizeof(entry)) == 0
           ? BL_FAULT_SAVED : BL_FAULT_SAVE_FAILED;
}
