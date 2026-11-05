#ifndef NVM_META_H_
#define NVM_META_H_

#include <stddef.h>

typedef struct NvmMeta NvmMeta;

// Base object: an ID belongs to a Manager slot, never to this object.
// vData is a paired backing: the caller passes the WHOLE extent to
// the ctor, which derives sSize = size/2; the first half is the live
// block, the second half is reserved for the backend's IO staging
// (fully static — no scratch pool, no cap).
struct NvmMeta {
    void *vData;
    size_t sSize;
};

// Bind caller-owned backing RAM. `size` is the FULL backing extent —
// nonzero and even — and the ctor derives the logical block size as
// size/2. Fresh zero state comes from the backing's storage duration
// (file-static instances are .bss-cleared); the ctor does not clear.
// data is required; no allocation or storage IO. The caller may
// assign nonzero defaults after construction.
void NvmMeta_ctor(NvmMeta *me, void *data, size_t size);

#endif /* NVM_META_H_ */
