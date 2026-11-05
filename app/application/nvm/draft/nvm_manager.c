// The manager module: the fs-kind vtable surface, this application's
// nvs backend + board window, and its default slot table. The module
// owns no NvmManager object and names no concrete instance — the
// instance roster is driven by the owner (the Flash AO assembly).
#include "nvm_manager.h"
#include "nvm_conf.h"
#include "nvm_port.h"
#include "nor_partitions.h"
#include "nvs.h"
#include "nvs_port.h"
#include "sm_tracer.h"
#include <errno.h>
#include <string.h>
#include <sys/types.h>

NVM_DEFINE_MODULE("nvm_manager")

// This application's slot table; ctor binds it (with its capacity)
// into each NvmManager object — a second manager may bind its own.
static NvmMeta *NvmManager_registry_[NVM_REGISTER_MAX];

// idNum derives from the array, so the table must at least cover the
// configured namespace — a bigger table is harmless; pin it here.
_Static_assert(sizeof(NvmManager_registry_)
               / sizeof(NvmManager_registry_[0]) >= NVM_REGISTER_MAX,
               "registry must cover NVM_REGISTER_MAX");

// Slot ids are uint16_t end to end (mountBlock parameter, persisted
// ID);
// the restore loop's uint16 counter wraps at idNum == 0x10000, so the
// namespace must stay strictly below that bound.
_Static_assert(NVM_REGISTER_MAX < 0x10000U,
               "slot ids are uint16_t");

// The nvs instance this application's backend serves; bound into each
// NvmManager at ctor.
static struct nvs_fs fs_;

static int NvsBackend_init_(NvmManager *me) {
    return nvs_mount((struct nvs_fs *)me->fs);
}

static int NvsBackend_load_(NvmManager *me, uint16_t id,
                            void *vData, size_t sSize) {
    // The block's upper half stages the IO: flash fills it, then the
    // live half is committed under the RAM section.
    void * const stage = (uint8_t *)vData + sSize;
    ssize_t const count = nvs_read((struct nvs_fs *)me->fs,
                                   id, stage, sSize);

    if (count == (ssize_t)sSize) {
        NVM_PORT_ENTER();
        memcpy(vData, stage, sSize);
        NVM_PORT_EXIT();
        return 0;
    }
    return (count < 0) ? (int)count : -EIO;
}

static int NvsBackend_store_(NvmManager *me, uint16_t id,
                             const void *vData, size_t sSize) {
    // Snapshot the live half into the block's upper half under the RAM
    // section; the on-flash image then cannot mix states during IO.
    void * const stage = (uint8_t *)vData + sSize;

    NVM_PORT_ENTER();
    memcpy(stage, vData, sSize);
    NVM_PORT_EXIT();
    ssize_t const count = nvs_write((struct nvs_fs *)me->fs,
                                    id, stage, sSize);
    return (count == 0 || count == (ssize_t)sSize)
           ? 0 : ((count < 0) ? (int)count : -EIO);
}

static NvmManagerVtbl const nvsBackendVtbl_ = {
    .init = NvsBackend_init_,
    .load = NvsBackend_load_,
    .store = NvsBackend_store_
};

// == DEBUG: per-slot restore failure; UINT16 slot ID, INT32 error. ==
// Inherits record 10 from the module this shape replaces.
#define NVM_TRACE_RESTORE_ERROR 10U
static void NvmManager_dbgRestoreError_(uint16_t id, int error) {
    uint32_t const bits = (uint32_t)(int32_t)error;
    SM_TRACE_BEGIN(NVM_TRACE_RESTORE_ERROR, 6)
    SM_TRACE_PAYLOAD_BE((uint16_t)id >> 8)
    SM_TRACE_PAYLOAD_BE((uint16_t)id)
    SM_TRACE_PAYLOAD_BE(bits >> 24)
    SM_TRACE_PAYLOAD_BE(bits >> 16)
    SM_TRACE_PAYLOAD_BE(bits >> 8)
    SM_TRACE_PAYLOAD_BE(bits)
    SM_TRACE_END(0)
}

void NvmManager_ctor(NvmManager *me) {
    NVM_REQUIRE(1, me != NULL);
    // Flat assembly: bind this application's slot table, fill the
    // board fs window, then mount the instance — the fs binding runs
    // only through the verb, never as a bare field poke here.
    // The instance roster is the owner's next call — each
    // NvmXxx_ctor(me) mounts its singleton into me's slots.
    me->nvmRegister = NvmManager_registry_;
    me->idNum = sizeof(NvmManager_registry_)
                / sizeof(NvmManager_registry_[0]);
    // The bound table must fit the uint16 id model wherever it comes
    // from — a custom rebind is not compile-pinned.
    NVM_REQUIRE(15, me->idNum < 0x10000U);
    fs_.offset = 0;
    fs_.sector_size = FLASH_DEV_SECTOR;
    fs_.sector_count = NOR_NVS_SECTORS;
    fs_.flash_device = nvs_port_device();
    NvmManager_mountFs(me, &fs_);
}

void NvmManager_mountFs(NvmManager *me, void *fs) {
    NVM_REQUIRE(16, me != NULL);
    NVM_REQUIRE(17, fs != NULL);
    me->vptr = &nvsBackendVtbl_;
    me->fs = fs;
}

void NvmManager_mountBlock(NvmManager *me, uint16_t id,
                           NvmMeta *meta) {
    NVM_REQUIRE(2, me != NULL);
    NVM_REQUIRE(3, meta != NULL);
    NVM_REQUIRE(13, meta->vData != NULL);
    NVM_REQUIRE(4, id < me->idNum);
    NVM_REQUIRE(5, me->nvmRegister[id] == NULL);
    me->nvmRegister[id] = meta;
}

// The full slot restore — file-private: this application loads only
// at init, so the pass folds into NvmManager_init rather than being
// a separate verb. -ENOENT keeps constructed defaults and is not an
// error; every other failure is traced and the first is returned.
static int NvmManager_restore_(NvmManager *me) {
    int result = 0;

    for (uint16_t id = 0U; id < me->idNum; ++id) {
        if (me->nvmRegister[id] == NULL) {
            continue;
        }
        int const loaded = NvmManager_load(me, id);
        if (loaded != 0 && loaded != -ENOENT) {
            // Missing records keep defaults; real failures are reported.
            NvmManager_dbgRestoreError_(id, loaded);
            if (result == 0) {
                result = loaded;
            }
        }
    }
    return result;
}

int NvmManager_init(NvmManager *me) {
    NVM_REQUIRE(6, me != NULL);
    // A zeroed (unconstructed) manager must not reach the wild call —
    // load/store/mountBlock are covered by the range assert; vptr is
    // not.
    NVM_REQUIRE(14, me->vptr != NULL);
    int const mounted = me->vptr->init(me);

    return (mounted == 0) ? NvmManager_restore_(me) : mounted;
}

int NvmManager_load(NvmManager *me, uint16_t id) {
    NVM_REQUIRE(7, me != NULL);
    NVM_REQUIRE(9, id < me->idNum);
    NVM_REQUIRE(10, me->nvmRegister[id] != NULL);
    NvmMeta * const meta = me->nvmRegister[id];

    return me->vptr->load(me, id, meta->vData, meta->sSize);
}

int NvmManager_store(NvmManager *me, uint16_t id) {
    NVM_REQUIRE(8, me != NULL);
    NVM_REQUIRE(11, id < me->idNum);
    NVM_REQUIRE(12, me->nvmRegister[id] != NULL);
    NvmMeta * const meta = me->nvmRegister[id];

    return me->vptr->store(me, id, meta->vData, meta->sSize);
}
