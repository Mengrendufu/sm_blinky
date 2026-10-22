/* NVS uses the BSP task-wait API in the task that owns the flash.
 * Calls complete synchronously while hardware waits let the CPU go.
 * This port only maps NVS offsets, geometry and errors.
 */
#include "nvs_port.h"
#include "bsp_flash.h"

#include "nor_w25q128.h"       /* the part's geometry, from the datasheet */
#include "nor_partitions.h"    /* and where this board put the log */

#define NVS_FLASH_BASE   NOR_NVS_BASE   /* the NOR region NVS was given */
#define NVS_WBS          4U

static const struct flash_parameters nvs_flash_par_ = {
    NVS_WBS,
    FLASH_DEV_ERASED,   /* what an erased byte reads back as */
};

void *nvs_port_device(void) {
    return (void *)0;
}

int flash_read(void *dev, off_t off, void *buf, size_t len) {
    (void)dev;
    if (!BSP_flashReadWait(NVS_FLASH_BASE + (uint32_t)off,
                               buf, (uint32_t)len)) {
        return -5;
    }
    return 0;
}

int flash_write(void *dev, off_t off, const void *buf, size_t len) {
    (void)dev;
    if ((len == 0U) || ((len % NVS_WBS) != 0U)) {
        return -22;
    }

    if (!BSP_flashWriteWait(NVS_FLASH_BASE + (uint32_t)off,
                                buf, (uint32_t)len)) {
        return -5;
    }
    return 0;
}

int flash_flatten(void *dev, off_t off, size_t len) {
    size_t done;

    (void)dev;
    for (done = 0U; done < len; done += FLASH_DEV_SECTOR) {
        if (!BSP_flashEraseSectorWait(NVS_FLASH_BASE + (uint32_t)off
                                         + (uint32_t)done)) {
            return -5;
        }
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
