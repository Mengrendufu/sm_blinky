/* The six functions of nvs_port.h, in the task that owns the bus.
 *
 * This file runs inside the flash thread (see Flash_nvsBoot_ in flash.c): the
 * NVS is a capability of that AO, so there is no request/response in the way
 * here - the primitives below are the same ones the AO's own operations use.
 *
 * Two things are load bearing and easy to get wrong:
 *   - the page split in flash_write. BSP_flashProgramPage writes one page and
 *     the W25Q128 wraps a crossing program around to the start of the page,
 *     silently. The split loop is the whole defence.
 *   - the erase lets the CPU go. BSP_flashEraseSectorStart plus a poll of
 *     BSP_flashIsReady with vTaskDelay is what keeps the idle task (and the
 *     tracer pump in it) alive through the 167 ms of device time. The blocking
 *     BSP_flashEraseSector would spin instead, which was measured to starve
 *     idle long ago.
 */
#include "nvs_port.h"
#include "bsp.h"
#include "bsp_flash.h"

#define NVS_FLASH_BASE   0x00100000U   /* the NOR region NVS was given */
#define NVS_WBS          4U
#define NVS_ERASE_POLL_MS 2U
#define NVS_ERASE_DEADLINE_MS 3000U

static const struct flash_parameters nvs_flash_par_ = {
    NVS_WBS,
    FLASH_DEV_ERASED,   /* what an erased byte reads back as */
};

void *nvs_port_device(void) {
    return (void *)0;
}

static void nvs_erase_let_go_(uint32_t address) {
    uint32_t waited = 0U;

    (void)BSP_flashEraseSectorStart(address);
    while (!BSP_flashIsReady()) {
        vTaskDelay(pdMS_TO_TICKS(NVS_ERASE_POLL_MS));
        waited += NVS_ERASE_POLL_MS;
        if (waited > NVS_ERASE_DEADLINE_MS) {
            break;
        }
    }
}

int flash_read(void *dev, off_t off, void *buf, size_t len) {
    (void)dev;
    if (!BSP_flashRead(NVS_FLASH_BASE + (uint32_t)off, (uint8_t *)buf,
                       (uint32_t)len)) {
        return -5;
    }
    return 0;
}

int flash_write(void *dev, off_t off, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t addr = NVS_FLASH_BASE + (uint32_t)off;

    (void)dev;
    if ((len == 0U) || ((len % NVS_WBS) != 0U)) {
        return -22;
    }

    while (len > 0U) {
        size_t chunk = FLASH_DEV_PAGE - (size_t)(addr % FLASH_DEV_PAGE);

        if (chunk > len) {
            chunk = len;
        }
        if (!BSP_flashProgramPage(addr, p, (uint32_t)chunk)) {
            return -5;
        }
        addr += (uint32_t)chunk;
        p += chunk;
        len -= chunk;
    }
    return 0;
}

int flash_flatten(void *dev, off_t off, size_t len) {
    size_t done;

    (void)dev;
    for (done = 0U; done < len; done += FLASH_DEV_SECTOR) {
        nvs_erase_let_go_(NVS_FLASH_BASE + (uint32_t)off + (uint32_t)done);
    }
    return 0;
}

const struct flash_parameters *flash_get_parameters(void *dev) {
    (void)dev;
    return &nvs_flash_par_;
}

size_t flash_get_write_block_size(void *dev) {
    (void)dev;
    return NVS_WBS;
}

int flash_get_page_info_by_offs(void *dev, off_t off,
                                struct flash_pages_info *info) {
    (void)dev;
    info->start_offset = (off_t)((uint32_t)off & ~(FLASH_DEV_SECTOR - 1U));
    info->size = FLASH_DEV_SECTOR;
    info->index = (uint32_t)(info->start_offset / (off_t)FLASH_DEV_SECTOR);
    return 0;
}
