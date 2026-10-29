#ifndef HDLC_FRAME_H_
#define HDLC_FRAME_H_
#include <stddef.h>
#include <stdint.h>

// Requires worst-case capacity: 2 * (payload length + 4) + 2 bytes.
// Returns encoded size, or zero if the supplied output capacity is short.
size_t HdlcFrame_encode(uint8_t sequence, uint8_t id,
                        uint8_t const *payload, uint8_t len,
                        uint8_t *wire, size_t capacity);
#endif
