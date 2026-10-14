/* ===== nvs_port.h — the whole platform contract =====
 * Top of zephyr/drivers/flash.h that the core actually uses: six functions.
 * Everything else the core needed from Zephyr is a no-op or a 20-line helper.
 * The requirements these six must meet (NVS assumes, it does not check):
 *   1. read: any offset, any length (8-byte ATE reads are normal)
 *   2. write: any offset, length a multiple of write_block_size, 1..32
 *   3. erase: sector granularity; sector_size % erase granularity == 0
 *   4. erased bytes read back as flash_parameters.erase_value
 *   5. program only clears bits (1 -> 0); never read-modify-write
 *   6. failures return a negative number, never silently
 *   7. the area is exclusive and every access comes from one execution body
 *   8. THE driver splits writes at page boundaries (see nvs_flash_port.c)
 */
#ifndef NVS_PORT_H_
#define NVS_PORT_H_

#include <sys/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "nvs_compat.h"



struct flash_parameters {
    size_t write_block_size;
    uint8_t erase_value;
};

struct flash_pages_info {
    off_t start_offset;
    size_t size;
    uint32_t index;
};

void *nvs_port_device(void);
int flash_read(void *dev, off_t off, void *buf, size_t len);
int flash_write(void *dev, off_t off, const void *buf, size_t len);
int flash_flatten(void *dev, off_t off, size_t len);
const struct flash_parameters *flash_get_parameters(void *dev);
size_t flash_get_write_block_size(void *dev);
int flash_get_page_info_by_offs(void *dev, off_t off,
                                struct flash_pages_info *info);

/* Optional critical section, for builds with more than one caller. The
 * sm_blinky build has one (the flash task) and compiles them to nothing. */
#ifndef NvsPort_critInit
#define NvsPort_critInit() ((void)0)
#endif
#ifndef NvsPort_critEnter
#define NvsPort_critEnter() ((void)0)
#endif
#ifndef NvsPort_critExit
#define NvsPort_critExit() ((void)0)
#endif

/* What Zephyr's Kconfig would have decided for this component. */
#define CONFIG_NVS_LOG_LEVEL 0
#define CONFIG_NVS_DATA_CRC 1
#define IS_ENABLED(cfg) (!!(cfg))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#endif /* NVS_PORT_H_ */
