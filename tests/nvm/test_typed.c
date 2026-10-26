// The runner registers this fixture in temporary copies of nvm.h/nvm.c.
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvm.h"
#include "nvs.h"

unsigned Host_critical;
static unsigned scenario;
static unsigned char expected[sizeof(Nvm_TestData)];
static struct nvs_fs store;

ssize_t nvs_read(struct nvs_fs *fs, uint16_t id, void *dst, size_t size) {
    assert(fs == &store && !Host_critical);
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
    assert(fs == &store && !Host_critical && id == NVM_TEST_RECORD);
    Nvm_Meta const *meta = Nvm_meta(NVM_TEST_RECORD);
    assert(size == sizeof(Nvm_TestData) && src != meta->data);
    assert(memcmp(src, expected, size) == 0);
    // Concurrent application changes must not alter the borrowed IO buffer.
    memset(meta->data, 0x33, size);
    assert(memcmp(src, expected, size) == 0);
    return (ssize_t)size;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    scenario = (unsigned)atoi(argv[1]);
    memset(expected, 0x5A, sizeof expected);
    Nvm_Meta const *meta = Nvm_meta(NVM_TEST_RECORD);
    assert(meta);
    assert(meta->size == sizeof(Nvm_TestData));
    assert(Nvm_load(&store) == (scenario == 2 ? -EIO : 0));
    unsigned char zero[sizeof(Nvm_TestData)] = {0};
    bool const failed = scenario == 1 || scenario == 2;
    assert(memcmp(meta->data, failed ? zero : expected, meta->size) == 0);
    memcpy(meta->data, expected, meta->size);
    assert(Nvm_store(&store, NVM_TEST_RECORD) == 0);
    assert(Nvm_writeResult(NVM_TEST_RECORD) == 0);
    assert(!Host_critical);
    printf("PASS typed NVM %u: %zu-byte struct/array, restore, stable IO\n",
           scenario, meta->size);
}
