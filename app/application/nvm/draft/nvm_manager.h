#ifndef NVM_MANAGER_H_
#define NVM_MANAGER_H_

#include "nvm_meta.h"
#include <stddef.h>
#include <stdint.h>

typedef struct NvmManager NvmManager;

// The fs-kind vtable — the whole polymorphism surface lives here.
// Ops receive me and reach their concrete fs through me->fs; no subtype
// wrapper is needed because the fs handle is the only context a backend
// of this kind carries. Zero on success, negative errno on failure;
// missing records report -ENOENT from load.
typedef int (*NvmManager_InitOp)(NvmManager *me);
typedef int (*NvmManager_LoadOp)(NvmManager *me, uint16_t id,
                                 void *vData, size_t sSize);
typedef int (*NvmManager_StoreOp)(NvmManager *me, uint16_t id,
                                  const void *vData, size_t sSize);

typedef struct {
    NvmManager_InitOp init;
    NvmManager_LoadOp load;
    NvmManager_StoreOp store;
} NvmManagerVtbl;

// A manager object: one backend vtable, the concrete fs handle, and
// the slot table pointer + element count it serves. The object is
// embedded in the caller's side — in this application the Flash AO —
// so the chain reads AO ⊃ NvmManager ⊃ fs. Today's ctor binds the
// module's default table/backend unconditionally; an independent
// namespace needs a rebind, parameterized when the need is real.
struct NvmManager {
    NvmManagerVtbl const *vptr;
    void *fs;
    NvmMeta **nvmRegister;
    size_t idNum;
};

// ctor binds me's slot table and mounts the board's nvs instance —
// this application's default assembly, expressed through mountFs.
// The instance roster is NOT run here: the owner calls each
// NvmXxx_ctor(me) so the manager never names a concrete instance —
// the dependency stays one-way, instances→manager.
void NvmManager_ctor(NvmManager *me);

// Mount a configured fs instance under me — object-level binding,
// not the on-flash volume mount (that is init's nvs_mount). The fs
// handle and the ops that drive it are bound in one act; the handle
// is void like me->fs — concretely this application passes its
// struct nvs_fs. ctor mounts this application's file-static
// instance; call again with another fs object to rebind — that is
// how a second manager binds its own region. The fs object's
// window fields are its owner's configuration and must already be
// filled.
void NvmManager_mountFs(NvmManager *me, void *fs);

// Mount a block into me's slot table; instance ctors call this with the
// manager they were constructed for. The slot index is the persisted
// ID; NULL slots mean unmounted. Asserts on an unbound meta, an
// out-of-range id and an occupied slot.
void NvmManager_mountBlock(NvmManager *me, uint16_t id,
                           NvmMeta *meta);

// One filesystem mount attempt plus the full slot restore — this
// application loads only at init, so the restore pass folds in rather
// than being a verb: -ENOENT keeps constructed defaults; every other
// slot failure is traced and the first is returned. Call exactly once
// on the owner's assembly — a second call re-restores and clobbers
// live blocks; the caller owns the retry-after-failure policy.
int NvmManager_init(NvmManager *me);

// Resolve the slot, then dispatch load/store through me->vptr directly
// against vData. The id must name a mounted slot — out-of-range and
// unmounted ids are caller bugs (REQUIRE), not runtime conditions.
// Returns are backend errno propagated as-is (-ENOENT missing record
// and -EIO short IO are the common ones). load is the restore pass's
// per-slot primitive — not for runtime use.
int NvmManager_load(NvmManager *me, uint16_t id);
int NvmManager_store(NvmManager *me, uint16_t id);

// Block IO is staged in the block's own RAM: every vData backing is
// 2×sSize and the backend ops use the upper half as the flash staging
// area, so flash never touches the live half. The RAM-side commit or
// sample runs under the port's RAM section (NVM_PORT_* in nvm_port.h),
// same as instance ops. Readers take the live block by pointer — the
// no-tear claim covers aligned word reads; multi-word fields would
// need a reader-side guard. Storage IO ordering stays with whoever
// drives the calls.

#endif /* NVM_MANAGER_H_ */
