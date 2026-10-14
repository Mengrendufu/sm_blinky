/* The six functions of nvs_port.h, over this project's flash task.
 *
 * Read, write and erase all go through Flash_readBlock/WriteBlock/EraseSector,
 * which are requests into the flash active object: the AO owns the SPI bus, so
 * "one execution body touches flash" stays true and NVS itself only has to be
 * called from one thread (see nvs_port.h, requirement 7).
 *
 * Page splitting is not repeated here on purpose: Sm_Flash_writeDma_ already
 * splits at page boundaries (measured on the board: 5 bytes in-page and 300
 * bytes across pages read back byte for byte). A cross-page program would wrap
 * around silently on the W25Q128, so that split is load bearing - it just
 * lives one layer down, where the DMA already has to know about it.
 */
#include "nvs_port.h"
#include "flash.h"

#define NVS_FLASH_BASE   0x00100000U   /* the NOR region NVS was given */
#define NVS_ERASE_SIZE   4096U
#define NVS_PAGE_SIZE    256U
#define NVS_WBS          4U

static const struct flash_parameters nvs_flash_par_ = {
    NVS_WBS,
    0xFFU,
};

void *nvs_port_device(void) {
    return (void *)0;
}

int flash_read(void *dev, off_t off, void *buf, size_t len) {
    (void)dev;
    if (!Flash_readBlock(NVS_FLASH_BASE + (uint32_t)off, buf,
                         (uint32_t)len)) {
        return -5;
    }
    return 0;
}

int flash_write(void *dev, off_t off, const void *buf, size_t len) {
    (void)dev;
    if ((len == 0U) || ((len % NVS_WBS) != 0U)) {
        return -22;
    }
    if (!Flash_writeBlock(NVS_FLASH_BASE + (uint32_t)off, buf,
                          (uint32_t)len)) {
        return -5;
    }
    return 0;
}

int flash_flatten(void *dev, off_t off, size_t len) {
    size_t done;

    (void)dev;
    for (done = 0U; done < len; done += NVS_ERASE_SIZE) {
        if (!Flash_eraseSector(NVS_FLASH_BASE + (uint32_t)off
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
    info->start_offset = (off_t)((uint32_t)off & ~(NVS_ERASE_SIZE - 1U));
    info->size = NVS_ERASE_SIZE;
    info->index = (uint32_t)(info->start_offset / (off_t)NVS_ERASE_SIZE);
    return 0;
}
