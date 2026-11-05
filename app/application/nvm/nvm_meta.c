#include "nvm_meta.h"
#include "nvm_port.h"

NVM_DEFINE_MODULE("nvm_meta")

void NvmMeta_ctor(NvmMeta *me, void *data, size_t size) {
    NVM_REQUIRE(1, me != NULL);
    NVM_REQUIRE(2, data != NULL);
    NVM_REQUIRE(3, size > 0U);
    NVM_REQUIRE(4, (size & 1U) == 0U);
    me->vData = data;
    // The backing is a pair; the logical block is its first half.
    me->sSize = size / 2U;
}
