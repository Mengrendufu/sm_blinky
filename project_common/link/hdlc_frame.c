#include "hdlc_frame.h"

static size_t HdlcFrame_byte_(uint8_t byte, uint8_t *wire) {
    if (byte == 0x7EU || byte == 0x7DU) {
        wire[0] = 0x7DU;
        wire[1] = byte ^ 0x20U;
        return 2U;
    }
    wire[0] = byte;
    return 1U;
}

size_t HdlcFrame_encode(uint8_t sequence, uint8_t id,
                        uint8_t const *payload, uint8_t len,
                        uint8_t *wire, size_t capacity) {
    if (capacity < 2U * ((size_t)len + 4U) + 2U) {
        return 0U;
    }
    uint8_t checksum = sequence + id + len;
    size_t used = 0U;
    wire[used++] = 0x7EU;
    used += HdlcFrame_byte_(sequence, wire + used);
    used += HdlcFrame_byte_(id, wire + used);
    used += HdlcFrame_byte_(len, wire + used);
    for (unsigned i = 0U; i < len; ++i) {
        checksum += payload[i];
        used += HdlcFrame_byte_(payload[i], wire + used);
    }
    used += HdlcFrame_byte_((uint8_t)~checksum, wire + used);
    wire[used++] = 0x7EU;
    return used;
}
