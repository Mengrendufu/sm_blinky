// Manager assembly, restore, post-restore operations and stable IO snapshots.
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "instance/nvm_boot.h"
#include "instance/nvm_blinky.h"
#include "nvm_port.h"
#include "nvs.h"

unsigned Host_critical;
static unsigned scenario, reads, writes, mounts;
static bool changeDuringWrite;
static ssize_t writeReturn = 4;
static uint16_t writtenKey;
static uint32_t writtenValue;
static struct nvs_fs *store;
static NvmManager manager;

void *nvs_port_device(void) { return &manager; }

int nvs_mount(struct nvs_fs *fs) {
    assert(!Host_critical && fs->flash_device == &manager);
    store = fs;
    if (scenario == 4 && mounts++ == 0) { return -EIO; }
    fs->ready = true;
    return 0;
}

ssize_t nvs_read(struct nvs_fs *fs, uint16_t key, void *dst, size_t size) {
    assert(fs == store && !Host_critical && size == 4);
    ++reads;
    if (scenario == 0) { return -ENOENT; }
    uint32_t value = (key == 1) ? 7 : 100;
    memcpy(dst, &value, size);
    if (key == 2 && scenario == 2) { return 2; }
    if (key == 2 && scenario == 3) { return -EIO; }
    return (ssize_t)size;
}

ssize_t nvs_write(struct nvs_fs *fs, uint16_t key, void const *src,
                  size_t size) {
    assert(fs == store && !Host_critical && size == 4);
    assert(src != (void const *)NvmBlinky_get());
    ++writes;
    writtenKey = key;
    memcpy(&writtenValue, src, size);
    if (changeDuringWrite) {
        changeDuringWrite = false;
        NvmBlinky_add(7);
        assert(memcmp(src, &writtenValue, size) == 0);
    }
    return writeReturn;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    scenario = (unsigned)atoi(argv[1]);
    assert(NVM_BOOT_ID == 1 && NVM_BLINKY_ID == 2);
    NvmManager_ctor(&manager);
    NvmBoot_ctor(&manager);
    NvmBlinky_ctor(&manager);
    int loaded = NvmManager_init(&manager);
    if (scenario == 4) {
        assert(loaded == -EIO && reads == 0 && !store->ready);
        loaded = NvmManager_init(&manager);
        assert(mounts == 2);
    }
    assert(reads == 2 && writes == 0);
    assert(loaded == ((scenario == 2 || scenario == 3) ? -EIO : 0));
    NvmBoot_inc();
    NVM_PORT_ENTER();
    assert(NvmBoot_get()->count == (scenario == 0 ? 1 : 8));
    NVM_PORT_EXIT();
    bool const defaults = scenario == 0 || scenario == 2 || scenario == 3;
    NvmBlinky_add(5);
    uint32_t const expected = defaults ? 5 : 105;
    NVM_PORT_ENTER();
    assert(NvmBlinky_get()->edges == expected);
    NVM_PORT_EXIT();
    changeDuringWrite = true;
    assert(NvmManager_store(&manager, NVM_BLINKY_ID) == 0);
    assert(writtenKey == 2 && writtenValue == expected);
    NVM_PORT_ENTER();
    assert(NvmBlinky_get()->edges == expected + 7);
    NVM_PORT_EXIT();
    assert(NvmManager_store(&manager, NVM_BOOT_ID) == 0);
    NVM_PORT_ENTER();
    assert(writtenKey == 1 && writtenValue == NvmBoot_get()->count);
    NVM_PORT_EXIT();
    writeReturn = 0; // NVS skips an unchanged value; this is success.
    assert(NvmManager_store(&manager, NVM_BLINKY_ID) == 0);
    writeReturn = -ENOSPC;
    assert(NvmManager_store(&manager, NVM_BLINKY_ID) == -ENOSPC);
    writeReturn = 2;
    assert(NvmManager_store(&manager, NVM_BLINKY_ID) == -EIO);
    assert(writes == 5 && !Host_critical);
    printf("PASS NVM scenario %u: assembly, restore, snapshot, errno\n",
           scenario);
}
