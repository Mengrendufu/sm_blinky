// A structured block registers itself; no generated module copies.
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvm_manager.h"
#include "nvs.h"

unsigned Host_critical;
enum { NVM_TEST_RECORD = 3U };
typedef struct {
    uint64_t flags;
    uint8_t payload[17];
} Nvm_TestData;
static Nvm_TestData data[2];
static NvmMeta meta;
static NvmManager manager;
static unsigned scenario;
static unsigned char expected[sizeof(Nvm_TestData)];
static struct nvs_fs *store;

void *nvs_port_device(void) { return &manager; }
int nvs_mount(struct nvs_fs *fs) {
    store = fs;
    fs->ready = true;
    return 0;
}

ssize_t nvs_read(struct nvs_fs *fs, uint16_t id, void *dst, size_t size) {
    assert(fs == store && !Host_critical);
    memset(dst, 0, size);
    if (id == NVM_TEST_RECORD) {
        assert(size == sizeof(Nvm_TestData) && size > sizeof(uint32_t));
        memcpy(dst, expected, size);
        if (scenario == 1) { return -ENOENT; }
        if (scenario == 2) { return -EIO; }
    }
    return (ssize_t)size;
}

ssize_t nvs_write(struct nvs_fs *fs, uint16_t id, void const *src,
                  size_t size) {
    assert(fs == store && !Host_critical && id == NVM_TEST_RECORD);
    assert(size == sizeof(Nvm_TestData) && src == &data[1]);
    assert(memcmp(src, expected, size) == 0);
    // Concurrent application changes must not alter the borrowed IO buffer.
    memset(meta.vData, 0x33, size);
    assert(memcmp(src, expected, size) == 0);
    return (ssize_t)size;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    scenario = (unsigned)atoi(argv[1]);
    memset(expected, 0x5A, sizeof expected);
    NvmManager_ctor(&manager);
    NvmMeta_ctor(&meta, data, sizeof data);
    NvmManager_mountBlock(&manager, NVM_TEST_RECORD, &meta);
    assert(meta.sSize == sizeof(Nvm_TestData));
    assert(NvmManager_init(&manager) == (scenario == 2 ? -EIO : 0));
    unsigned char zero[sizeof(Nvm_TestData)] = {0};
    bool const failed = scenario == 1 || scenario == 2;
    assert(memcmp(meta.vData, failed ? zero : expected, meta.sSize) == 0);
    memcpy(meta.vData, expected, meta.sSize);
    assert(NvmManager_store(&manager, NVM_TEST_RECORD) == 0);
    assert(!Host_critical);
    printf("PASS typed NVM %u: %zu-byte struct/array, restore, stable IO\n",
           scenario, meta.sSize);
}
