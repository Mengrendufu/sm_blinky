// Shared on-flash format. All fields are big-endian; no native structs
// are serialized. One complete package is burned at a time.
#ifndef ASSET_FORMAT_H_
#define ASSET_FORMAT_H_

#include <stdbool.h>
#include <stdint.h>

#define ASSET_FORMAT_MAGIC       0x41535432U  // AST2
#define ASSET_FORMAT_VERSION     2U
#define ASSET_FORMAT_SECTOR      4096U
#define ASSET_FORMAT_SLOT_BYTES  153600U
#define ASSET_FORMAT_SLOTS       3U
#define ASSET_FORMAT_ENTRY_BYTES 16U
#define ASSET_FORMAT_HEADER      68U
#define ASSET_FORMAT_CRC_OFFSET  64U
#define ASSET_FORMAT_MAX_PAYLOAD \
    (ASSET_FORMAT_SLOTS * ASSET_FORMAT_SLOT_BYTES)
#define ASSET_FORMAT_MAX_PACKAGE \
    (ASSET_FORMAT_SECTOR + ASSET_FORMAT_MAX_PAYLOAD)

// Header: magic, version, count, payload length, three directory entries,
// then CRC32 of the preceding 64 bytes. Unused entries must be zero.
// Entry: ID, payload-relative offset, length, CRC32 of stored image bytes.
// Current IDs equal slot indices; Splash is ID 2. Slots remain fixed size.
static inline uint32_t AssetFormat_be32(uint8_t const *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
        | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline uint32_t AssetFormat_crcUpdate(uint32_t crc,
                                            uint8_t const *data,
                                            uint32_t len) {
    crc ^= 0xFFFFFFFFU;
    for (uint32_t i = 0U; i < len; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            crc = ((crc & 1U) != 0U)
                ? ((crc >> 1) ^ 0xEDB88320U) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

static inline uint8_t const *AssetFormat_entry(uint8_t const *header,
                                              uint32_t slot) {
    return &header[16U + slot * ASSET_FORMAT_ENTRY_BYTES];
}

static inline bool AssetFormat_valid(uint8_t const *header) {
    uint32_t const count = AssetFormat_be32(&header[8]);

    if ((AssetFormat_be32(header) != ASSET_FORMAT_MAGIC)
        || (AssetFormat_be32(&header[4]) != ASSET_FORMAT_VERSION)
        || (count == 0U) || (count > ASSET_FORMAT_SLOTS)
        || (AssetFormat_be32(&header[12])
            != count * ASSET_FORMAT_SLOT_BYTES)
        || (AssetFormat_crcUpdate(0U, header, ASSET_FORMAT_CRC_OFFSET)
            != AssetFormat_be32(&header[ASSET_FORMAT_CRC_OFFSET]))) {
        return false;
    }
    for (uint32_t i = 0U; i < ASSET_FORMAT_SLOTS; ++i) {
        uint8_t const * const entry = AssetFormat_entry(header, i);
        if (i < count) {
            if ((AssetFormat_be32(entry) != i)
                || (AssetFormat_be32(&entry[4])
                    != i * ASSET_FORMAT_SLOT_BYTES)
                || (AssetFormat_be32(&entry[8])
                    != ASSET_FORMAT_SLOT_BYTES)) {
                return false;
            }
        } else {
            for (uint32_t j = 0U; j < ASSET_FORMAT_ENTRY_BYTES; ++j) {
                if (entry[j] != 0U) {
                    return false;
                }
            }
        }
    }
    return true;
}

#endif // ASSET_FORMAT_H_
