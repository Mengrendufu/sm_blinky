#ifndef ASSET_LINK_H_
#define ASSET_LINK_H_

// Link IDs occupy a separate namespace from diagnostic record IDs.
#define ASSET_LINK_REQUEST 0xF0U
#define ASSET_LINK_REPLY   0xF1U
#define ASSET_LINK_TEXT    0xF2U
#define ASSET_LINK_CONTROL_MAX 10U
#define ASSET_LINK_REPLY_MAX 14U // request kind/index + old handshake ACK

// Reply payload: request kind, uint16 block index, legacy answer bytes.
// Handshake/final use index zero. Requests retain their inner CRC32 checks.
#endif
