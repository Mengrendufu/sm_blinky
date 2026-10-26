// Table/key compatibility, restore races, per-item IO and write results.
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvm.h"
#include "nvs.h"

unsigned Host_critical;
static unsigned scenario, reads, writes;
static bool changeDuringWrite;
static ssize_t writeReturn = 4;
static uint16_t writtenKey;
static uint32_t writtenValue;
static struct nvs_fs store;

ssize_t nvs_read(struct nvs_fs *fs, uint16_t key, void *dst, size_t size) {
    assert(fs == &store && !Host_critical && size == 4);
    ++reads;
    if (scenario == 0) { return -ENOENT; }
    if (scenario == 4 && key == 1) { Nvm_addBlinky(3); }
    uint32_t value = (key == 1) ? 7 : 100;
    memcpy(dst, &value, size);
    if (key == 2 && scenario == 2) { return 2; }
    if (key == 2 && scenario == 3) { return -EIO; }
    return (ssize_t)size;
}

ssize_t nvs_write(struct nvs_fs *fs, uint16_t key, void const *src,
                  size_t size) {
    assert(fs == &store && !Host_critical && size == 4);
    ++writes;
    writtenKey = key;
    memcpy(&writtenValue, src, size);
    if (changeDuringWrite) {
        changeDuringWrite = false;
        Nvm_addBlinky(7);
    }
    return writeReturn;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    scenario = (unsigned)atoi(argv[1]);
    assert(NVM_BOOT_COUNT == 1 && NVM_BLINKY_EDGES == 2);
    assert(Nvm_writeResult(NVM_BLINKY_EDGES) == -EAGAIN);
    Nvm_addBlinky(5);
    int const loaded = Nvm_load(&store);
    assert(reads == 2 && writes == 0);
    assert(loaded == ((scenario == 2 || scenario == 3) ? -EIO : 0));
    assert(Nvm_bootCount() == (scenario == 0 ? 1 : 8));
    bool const defaults = scenario == 0 || scenario == 2 || scenario == 3;
    uint32_t const expected = defaults ? 5 : (scenario == 4 ? 108 : 105);
    assert(Nvm_blinky() == expected);
    changeDuringWrite = true;
    assert(Nvm_store(&store, NVM_BLINKY_EDGES) == 0);
    assert(writtenKey == 2 && writtenValue == expected);
    assert(Nvm_blinky() == expected + 7);
    assert(Nvm_writeResult(NVM_BLINKY_EDGES) == 0);
    assert(Nvm_store(&store, NVM_BOOT_COUNT) == 0);
    assert(writtenKey == 1 && writtenValue == Nvm_bootCount());
    writeReturn = 0; // NVS skips an unchanged value; this is success.
    assert(Nvm_store(&store, NVM_BLINKY_EDGES) == 0);
    assert(Nvm_writeResult(NVM_BLINKY_EDGES) == 0);
    writeReturn = -ENOSPC;
    assert(Nvm_store(&store, NVM_BLINKY_EDGES) == -ENOSPC);
    assert(Nvm_writeResult(NVM_BLINKY_EDGES) == -ENOSPC);
    writeReturn = 2;
    assert(Nvm_store(&store, NVM_BLINKY_EDGES) == -EIO);
    unsigned const before = writes;
    assert(Nvm_store(&store, NVM_ID_INVALID) == -EINVAL);
    assert(Nvm_store(&store, NVM_ID_LIMIT) == -EINVAL);
    assert(Nvm_writeResult((Nvm_Id)-1) == -EINVAL);
    assert(writes == before && !Host_critical);
    printf("PASS NVM scenario %u: mapping, restore, snapshot, results\n",
           scenario);
}
