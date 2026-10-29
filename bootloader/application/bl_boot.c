//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
//============================================================================
//=== Bootloader -- the first 16 KB of the flash
//
// It owns two things:
//
// * The cross-reset flag. The linker script reserves the top 32 bytes of RAM
//     (_snoinit) and the startup code does not clear them, so a word survives
//     a reset (not a power cycle). Writing 0xB00710AD there asks for the next
//     reset to land here instead of in the app: J-Link writes it from the PC
//     (the application asset-upload tool), and this program clears it
//     when the work is done.
// * The decision. Flag set, or a handshake inside the boot window, means the
//     board stays here and serves the asset protocol. Otherwise the app
//     gets the CPU.
//
// The hand-over is the part that has to be right. On a real reset the
// hardware
// loads MSP and PC out of the vector table; nothing does that for a jump we
// make ourselves, so VTOR, MSP and PC are set by hand below.
//
// Asset protocol and NOR verification are implemented below.
//============================================================================
#include "stm32f1xx_hal.h"
#include "bl_boot.h"
#include "bsp_flash.h"
#include "asset_format.h"
#include "project_layout.h"
#include "bl_fault_log.h"
#include "bl_link.h"
#include "asset_link.h"
#include "hdlc_parser.h"
#include "sm_hsm.h"
#include "nor_partitions.h"

#include <stdbool.h>
#include <string.h>

//============================================================================
// What the two linker scripts and the app's main() agree on.
#define BL_APP_BASE    PROJECT_APP_BASE
#define BL_FLASH_END   PROJECT_APP_END
#define BL_RAM_BASE    PROJECT_RAM_BASE
// 64 KB RAM; the linker reserves the upgrade flag and fault record.
#define BL_RAM_END     PROJECT_RAM_END

#define BL_FLAG_MAGIC  PROJECT_BURN_MAGIC
#define BL_BOOT_MS     200U         // handshake window for the RESET key

// Burn-mode handshake (step 3 of the plan: prove the wire, touch no flash).
//
//   request  PC -> BL:  AA | ver(1) | len(4 BE) | crc32(4 BE)     10 bytes
//                       crc32 covers ver+len (5 bytes)
//   reply    BL -> PC:  55 | base(4 BE) | block(2 BE) | crc32(4 BE)  11 bytes
//                       crc32 covers the first 7 bytes, so the PC can tell a
// real answer from noise on a line the trace also drives
//   refusal  BL -> PC:  4E
#define BL_REQ_MAGIC   0xAAU
#define BL_ACK_MAGIC   0x55U
#define BL_NAK_MAGIC   0x4EU
#define BL_REQ_LEN     10U
#define BL_ACK_LEN     11U   // 55 + base(4) + block(2) + crc32(4)
#define BL_PROTO_VER   ASSET_FORMAT_VERSION
#define BL_BLOCK_BYTES 4096U
#define BL_ASSET_MAX   ASSET_FORMAT_MAX_PACKAGE
#define BL_ASSET_BASE  NOR_ASSET_BASE
#define BL_ASSET_DATA  NOR_ASSET_DATA
#define BL_HDR_BYTES   ASSET_FORMAT_HEADER
#define BL_PAGE_BYTES  256U         // the device's program page

_Static_assert(BL_ASSET_BASE + BL_ASSET_MAX == NOR_ASSET_END,
               "bootloader and NOR asset ranges disagree");

// Protocol v2 transfers a directory sector followed by fixed-size images.
// Block zero is staged in RAM; NOR directory is invalidated before payload
// writes, then committed only after every stored image passes its own CRC.
// The final package CRC also protects the complete transport stream.
//
//   block    PC -> BL:  B1 | idx(2 BE) | len(2 BE) | crc32低16(2 BE)   7 B
//                       then len bytes of payload
//   final    PC -> BL:  C1 | total(4 BE) | crc32(4 BE)                  9 B
//   answer   BL -> PC:  55 (taken) / 4E (rejected)
#define BL_BLK_MAGIC   0xB1U
#define BL_BLK_HDR     7U
#define BL_FIN_MAGIC   0xC1U
#define BL_FIN_LEN     9U
#define BL_MAX_BLOCK   4096U

// Written by J-Link, cleared here. Same address in both images (see the .ld).
extern uint32_t _snoinit;

// Burn-mode counters. Global on purpose: a J-Link probe reads them by name
// (arm-none-eabi-nm gives the addresses) while the target keeps running.
uint32_t Bl_frames_;    // frames that started with the marker byte
uint32_t Bl_ok_;        // frames that passed the CRC
uint32_t Bl_bad_;       // frames rejected: version, length or CRC
uint32_t Bl_len_;       // length field of the last accepted frame
uint32_t Bl_blocks_;    // blocks taken (payload CRC matched)
uint32_t Bl_retry_;     // blocks rejected - the PC is expected to send again
uint32_t Bl_bytes_;     // payload bytes folded into the running CRC
// running CRC32 over the payload (folded per good block)
uint32_t Bl_crcRun_;
uint32_t Bl_final_;     // 0 = no final frame yet, 1 = matched, 2 = mismatched
uint32_t Bl_last_;      // index of the last block taken
uint32_t Bl_written_;   // accepted blocks (directory staged, data verified)
uint32_t Bl_wrBad_;     // blocks whose erase/program/read-back failed

// Stop-and-wait retransmission acknowledges the last accepted block without
// writing it or folding it into the transport CRC twice.
// Raw asset data is owned by the transfer HSM, never fed to HDLC.
static uint8_t Bl_block_[BL_MAX_BLOCK];
static uint8_t Bl_page_[BL_PAGE_BYTES];      // read-back bytes
static uint8_t Bl_directory_[BL_HDR_BYTES];
static uint32_t Bl_lastCrc_;

static UART_HandleTypeDef *Bl_uart_;
static uint8_t Bl_requestKind_;
static uint16_t Bl_requestIndex_;

//============================================================================
static void Bl_say_(char const *text);
static bool Bl_appSane_(void);
static void Bl_handOver_(void) __attribute__((noreturn));
static uint32_t Bl_crc32_(uint8_t const *data, uint32_t len);
static uint32_t Bl_crc32Update_(uint32_t crc, uint8_t const *data,
                                uint32_t len);
static uint32_t Bl_be32_(uint8_t const *p);
static void Bl_putBe32_(uint8_t *p, uint32_t v);
static bool Bl_frameOk_(uint8_t const *frame);
static bool Bl_writeBlock_(uint32_t idx, uint32_t len);
static bool Bl_commit_(void);
static void Bl_ackFrame_(void);
static void Bl_ack1_(void);
static void Bl_nak_(void);

//............................................................................
static void Bl_say_(char const *text) {
    size_t left = strlen(text);
    while (left != 0U) {
        uint8_t const n = left > UINT8_MAX ? UINT8_MAX : (uint8_t)left;
        if (!Bl_linkSend(Bl_uart_, ASSET_LINK_TEXT,
                         (uint8_t const *)text, n)) {
            return;
        }
        text += n;
        left -= n;
    }
}
//............................................................................
// CRC32, the same one zlib.crc32() computes (poly 0xEDB88320, init all ones,
// final xor), so the PC side can use its standard library call. The update
// form exists because the payload arrives in blocks and a re-sent block must
// not be folded in twice.
static uint32_t Bl_crc32Update_(uint32_t crc, uint8_t const *data,
                                uint32_t len) {
    return AssetFormat_crcUpdate(crc, data, len);
}
//............................................................................
static uint32_t Bl_crc32_(uint8_t const *data, uint32_t len) {
    return Bl_crc32Update_(0U, data, len);
}
//............................................................................
static uint32_t Bl_be32_(uint8_t const *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
           | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
//............................................................................
static void Bl_putBe32_(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
//............................................................................
static bool Bl_frameOk_(uint8_t const *frame) {
    uint32_t const ver = frame[1];
    uint32_t const len = Bl_be32_(&frame[2]);
    uint32_t const crc = Bl_be32_(&frame[6]);

    if (ver != BL_PROTO_VER) {
        return false;
    }
    if ((len <= ASSET_FORMAT_SECTOR) || (len > BL_ASSET_MAX)
        || (((len - ASSET_FORMAT_SECTOR) % ASSET_FORMAT_SLOT_BYTES)
            != 0U)) {
        return false;
    }

    return crc == Bl_crc32_(&frame[1], 5U);
}
//............................................................................
// Reply routing is independent of diagnostic record IDs. Echo request
// kind/index so a delayed ACK cannot acknowledge a different block.
static void Bl_reply_(uint8_t const *answer, uint8_t len) {
    uint8_t payload[ASSET_LINK_REPLY_MAX] = {
        Bl_requestKind_, (uint8_t)(Bl_requestIndex_ >> 8),
        (uint8_t)Bl_requestIndex_
    };
    memcpy(payload + 3U, answer, len);
    (void)Bl_linkSend(Bl_uart_, ASSET_LINK_REPLY, payload, len + 3U);
}
//............................................................................
// The handshake answer: 11 bytes with its own CRC.
static void Bl_ackFrame_(void) {
    uint8_t out[BL_ACK_LEN];

    out[0] = BL_ACK_MAGIC;
    Bl_putBe32_(&out[1], BL_ASSET_BASE);
    out[5] = (uint8_t)(BL_BLOCK_BYTES >> 8);
    out[6] = (uint8_t)(BL_BLOCK_BYTES & 0xFFU);
    Bl_putBe32_(&out[7], Bl_crc32_(out, 7U));

    Bl_reply_(out, BL_ACK_LEN);
}
//............................................................................
// Block/final answer is one status byte inside the typed HDLC reply.
// The handshake answer retains its 11-byte body and CRC32.
static void Bl_ack1_(void) {
    uint8_t n = BL_ACK_MAGIC;

    Bl_reply_(&n, 1U);
}
//............................................................................
static void Bl_nak_(void) {
    uint8_t n = BL_NAK_MAGIC;

    Bl_reply_(&n, 1U);
}
//............................................................................
// Bare-metal wait capabilities: DMA completion is latched, then consumed
// by a busy wait. Burn mode runs in main with interrupts and HAL tick live.
typedef struct {
    volatile bool done;
} Bl_FlashWait;

static Bl_FlashWait Bl_flashWait_;

static bool Bl_flashIsCaller_(void *context) {
    return (context == &Bl_flashWait_) && (__get_IPSR() == 0U);
}

static bool Bl_flashWaitDone_(void *context, uint32_t timeoutMs) {
    Bl_FlashWait * const wait = (Bl_FlashWait *)context;
    uint32_t const started = HAL_GetTick();

    while (!wait->done) {
        if ((uint32_t)(HAL_GetTick() - started) >= timeoutMs) {
            return false;
        }
    }
    wait->done = false;
    return true;
}

static void Bl_flashNotifyDone_(void *context) {
    Bl_FlashWait * const wait = (Bl_FlashWait *)context;

    wait->done = true;
}

static void Bl_flashSleepMs_(void *context, uint32_t delayMs) {
    (void)context;
    HAL_Delay(delayMs);
}

static void Bl_flashYield_(void *context) {
    (void)context;
}

static uint32_t Bl_flashNow_(void *context) {
    (void)context;
    return HAL_GetTick();
}

static uint32_t Bl_flashElapsedMs_(void *context, uint32_t started) {
    (void)context;
    return HAL_GetTick() - started;
}

static void Bl_flashBindWait_(void) {
    static BSP_flashWaitOperations const operations = {
        .isCaller = &Bl_flashIsCaller_,
        .waitDone = &Bl_flashWaitDone_,
        .notifyDone = &Bl_flashNotifyDone_,
        .sleepMs = &Bl_flashSleepMs_,
        .yield = &Bl_flashYield_,
        .now = &Bl_flashNow_,
        .elapsedMs = &Bl_flashElapsedMs_,
    };

    BSP_flashBind(BSP_flashDevice(), &BSP_flashDmaOperations,
                   &operations, &Bl_flashWait_);
}

// Program one block into the NOR and read it back. Same bus, so the read-back
// is a check of what the device kept, not of what we think we sent.
static bool Bl_writeBlock_(uint32_t idx, uint32_t len) {
    uint32_t const addr = BL_ASSET_BASE + (idx * BL_MAX_BLOCK);
    uint32_t off;
    uint32_t n;

    if (!BSP_flashEraseSector(BSP_flashDevice(), addr)) {
        return false;
    }

    if (!BSP_flashWrite(BSP_flashDevice(), addr, Bl_block_, len)) {
        return false;
    }

    for (off = 0U; off < len; off += BL_PAGE_BYTES) {
        n = ((len - off) > BL_PAGE_BYTES) ? BL_PAGE_BYTES : (len - off);
        if (!BSP_flashRead(BSP_flashDevice(), addr + off, Bl_page_, n)) {
            return false;
        }
        if (memcmp(Bl_page_, &Bl_block_[off], n) != 0) {
            return false;
        }
    }

    return true;
}
//............................................................................
// Verify each image from NOR against the script-provided expected CRC.
// The directory sector was erased before the first payload write. This
// invalidates an interrupted replacement; it does not preserve the old pack.
static bool Bl_commit_(void) {
    uint32_t const count = AssetFormat_be32(&Bl_directory_[8]);

    for (uint32_t i = 0U; i < count; ++i) {
        uint8_t const * const entry = AssetFormat_entry(Bl_directory_, i);
        uint32_t const addr = BL_ASSET_DATA + AssetFormat_be32(&entry[4]);
        uint32_t const len = AssetFormat_be32(&entry[8]);
        uint32_t crc = 0U;

        for (uint32_t off = 0U; off < len; off += BL_PAGE_BYTES) {
            uint32_t const n = ((len - off) > BL_PAGE_BYTES)
                ? BL_PAGE_BYTES : (len - off);
            if (!BSP_flashRead(BSP_flashDevice(), addr + off, Bl_page_, n)) {
                return false;
            }
            crc = Bl_crc32Update_(crc, Bl_page_, n);
        }
        if (crc != AssetFormat_be32(&entry[12])) {
            return false;
        }
    }
    if (!BSP_flashWrite(BSP_flashDevice(), BL_ASSET_BASE,
                        Bl_directory_, BL_HDR_BYTES)
        || !BSP_flashRead(BSP_flashDevice(), BL_ASSET_BASE,
                          Bl_page_, BL_HDR_BYTES)) {
        return false;
    }
    return memcmp(Bl_page_, Bl_directory_, BL_HDR_BYTES) == 0;
}
//............................................................................
// Application actions. The transfer HSM owns mode changes and replies.
static bool Bl_acceptBlock_(uint32_t idx, uint32_t len, uint32_t lo16) {
    uint32_t const crc = Bl_crc32_(Bl_block_, len);
    if ((crc & 0xFFFFU) != lo16) {
        ++Bl_retry_;
        return false;
    }
    // Bound index/length to the negotiated complete package. The
    // last accepted block may be replayed after a lost ACK.
    uint32_t const offset = idx * BL_MAX_BLOCK;
    if ((Bl_len_ == 0U) || (offset >= Bl_len_)
        || (len != (((Bl_len_ - offset) > BL_MAX_BLOCK)
            ? BL_MAX_BLOCK : (Bl_len_ - offset)))) {
        return false;
    }
    if ((Bl_bytes_ != 0U) && (idx == Bl_last_)
        && (crc == Bl_lastCrc_)) {
        return true;
    }
    if (offset != Bl_bytes_) {
        return false;
    }
    if (idx == 0U) {
        if (!AssetFormat_valid(Bl_block_)
            || (AssetFormat_be32(&Bl_block_[12])
                + ASSET_FORMAT_SECTOR != Bl_len_)) {
            return false;
        }
        if (!BSP_flashEraseSector(BSP_flashDevice(), BL_ASSET_BASE)) {
            return false;
        }
        memcpy(Bl_directory_, Bl_block_, BL_HDR_BYTES);
    } else if (!Bl_writeBlock_(idx, len)) {
        ++Bl_wrBad_;
        return false;
    }

    // Folded in only after the write: a block that failed its CRC or
    // its write must not reach the running CRC, or the final check
    // could never match.
    Bl_crcRun_ = Bl_crc32Update_(Bl_crcRun_, Bl_block_, len);
    Bl_bytes_ += len;
    Bl_last_ = idx;
    Bl_lastCrc_ = crc;
    ++Bl_blocks_;
    ++Bl_written_;
    return true;
}

// UART bytes belong to exactly one of control/data/resync. Only control
// forwards bytes into HdlcParser. Commands are dispatched after the byte
// handler returns, avoiding reentrant dispatch from the parser's sink.
enum {
    BL_BYTE_SIG = 1, BL_GAP_SIG, BL_HELLO_SIG, BL_BLOCK_SIG,
    BL_FINISH_SIG, BL_BAD_COMMAND_SIG, BL_BOOT_COMMAND_SIG
};
typedef struct { uint8_t sig, byte; } Bl_TransferEvt;
static struct {
    SM_Hsm hsm;
    HdlcParser parser;
    uint8_t control[ASSET_LINK_CONTROL_MAX];
    uint8_t controlSize, command;
    uint16_t received, expected, index, crc16;
    bool started, content, frameSeen, frameUsable, frameControl;
    bool pending, nakPending;
} Bl_transfer_;

static SM_RetState Bl_control_(SM_Hsm *me, Bl_TransferEvt const *e);
static SM_RetState Bl_data_(SM_Hsm *me, Bl_TransferEvt const *e);
static SM_RetState Bl_resync_(SM_Hsm *me, Bl_TransferEvt const *e);
static void Bl_controlEntry_(SM_Hsm *me);
static void Bl_dataEntry_(SM_Hsm *me);
static SM_HsmState const Bl_control = {
    SM_HSM_TOP, NULL, (SM_ActionHandler)Bl_controlEntry_, NULL,
    (SM_StateHandler)Bl_control_
};
static SM_HsmState const Bl_data = {
    SM_HSM_TOP, NULL, (SM_ActionHandler)Bl_dataEntry_, NULL,
    (SM_StateHandler)Bl_data_
};
static SM_HsmState const Bl_resync = {
    SM_HSM_TOP, NULL, NULL, NULL, (SM_StateHandler)Bl_resync_
};

static void Bl_controlFrame_(void *context, uint8_t const *frame,
                              size_t size) {
    (void)context;
    Bl_transfer_.frameSeen = true;
    Bl_transfer_.frameControl = frame[1] == ASSET_LINK_REQUEST;
    Bl_transfer_.frameUsable = Bl_transfer_.frameControl
        && size > 3U && size <= 3U + sizeof(Bl_transfer_.control);
    if (Bl_transfer_.frameUsable) {
        Bl_transfer_.controlSize = (uint8_t)(size - 3U);
        memcpy(Bl_transfer_.control, frame + 3U, size - 3U);
    }
}
static void Bl_controlEntry_(SM_Hsm *me) {
    (void)me;
    HdlcParser_init(&Bl_transfer_.parser);
    Bl_transfer_.started = Bl_transfer_.content = false;
    Bl_transfer_.frameSeen = Bl_transfer_.frameUsable = false;
}
static void Bl_dataEntry_(SM_Hsm *me) {
    (void)me;
    Bl_transfer_.received = 0U;
}
static SM_StatePtr Bl_transferInitial_(SM_Hsm *me) {
    (void)me;
    return _SM_INIT(&Bl_control);
}

static SM_RetState Bl_control_(SM_Hsm *me, Bl_TransferEvt const *e) {
    uint8_t const *hdr = Bl_transfer_.control;
    switch (e->sig) {
        case BL_BYTE_SIG: {
            if (e->byte == 0x7EU && Bl_transfer_.content) {
                // Consume the closing flag before accepting any raw data.
                if (!Bl_transfer_.frameSeen
                    || (Bl_transfer_.frameControl
                        && !Bl_transfer_.frameUsable)) {
                    Bl_transfer_.nakPending = false;
                    return _SM_TRAN(&Bl_resync);
                }
                if (Bl_transfer_.frameUsable) {
                    Bl_requestKind_ = hdr[0];
                    Bl_requestIndex_ = 0U;
                    Bl_transfer_.command = hdr[0] == BL_REQ_MAGIC
                        ? BL_HELLO_SIG : hdr[0] == BL_BLK_MAGIC
                        ? BL_BLOCK_SIG : hdr[0] == BL_FIN_MAGIC
                        ? BL_FINISH_SIG : BL_BAD_COMMAND_SIG;
                    Bl_transfer_.pending = true;
                }
                Bl_transfer_.content = Bl_transfer_.frameSeen = false;
                Bl_transfer_.frameUsable = false;
            }
            else if (e->byte != 0x7EU && Bl_transfer_.started) {
                if (Bl_transfer_.frameSeen) {
                    Bl_transfer_.nakPending = false;
                    return _SM_TRAN(&Bl_resync);
                }
                Bl_transfer_.content = true;
            }
            if (e->byte == 0x7EU) { Bl_transfer_.started = true; }
            HdlcParser_input(&Bl_transfer_.parser, e->byte);
            return _SM_HANDLED();
        }
        case BL_GAP_SIG: {
            return _SM_TRAN(&Bl_control);
        }
        case BL_HELLO_SIG: {
            ++Bl_frames_;
            if (Bl_transfer_.controlSize != BL_REQ_LEN || !Bl_frameOk_(hdr)
                || (Bl_bytes_ != 0U && Bl_len_ != Bl_be32_(hdr + 2U))) {
                ++Bl_bad_;
                Bl_nak_();
                return _SM_HANDLED();
            }
            ++Bl_ok_;
            Bl_len_ = Bl_be32_(hdr + 2U);
            Bl_ackFrame_();
            return _SM_HANDLED();
        }
        case BL_BLOCK_SIG: {
            if (Bl_transfer_.controlSize != BL_BLK_HDR) {
                Bl_transfer_.nakPending = true;
                return _SM_TRAN(&Bl_resync);
            }
            Bl_transfer_.index = ((uint16_t)hdr[1] << 8) | hdr[2];
            Bl_requestIndex_ = Bl_transfer_.index;
            Bl_transfer_.expected = ((uint16_t)hdr[3] << 8) | hdr[4];
            Bl_transfer_.crc16 = ((uint16_t)hdr[5] << 8) | hdr[6];
            if (Bl_transfer_.expected == 0U
                || Bl_transfer_.expected > sizeof(Bl_block_)) {
                Bl_transfer_.nakPending = true;
                return _SM_TRAN(&Bl_resync);
            }
            return _SM_TRAN(&Bl_data);
        }
        case BL_FINISH_SIG: {
            if (Bl_transfer_.controlSize != BL_FIN_LEN || Bl_len_ == 0U
                || Bl_bytes_ != Bl_len_ || Bl_be32_(hdr + 1U) != Bl_len_
                || Bl_be32_(hdr + 5U) != Bl_crcRun_) {
                Bl_final_ = 2U;
                Bl_nak_();
                return _SM_HANDLED();
            }
            if (!Bl_commit_()) {
                Bl_final_ = 3U;
                Bl_nak_();
                return _SM_HANDLED();
            }
            Bl_final_ = 1U;
            _snoinit = 0U;
            Bl_ack1_();
            HAL_Delay(50U);
            NVIC_SystemReset();
            return _SM_HANDLED();
        }
        case BL_BAD_COMMAND_SIG: {
            Bl_transfer_.nakPending = true;
            return _SM_TRAN(&Bl_resync);
        }
        default: { return _SM_SUPER(); }
    }
}
static SM_RetState Bl_data_(SM_Hsm *me, Bl_TransferEvt const *e) {
    switch (e->sig) {
        case BL_BYTE_SIG: {
            Bl_block_[Bl_transfer_.received++] = e->byte;
            if (Bl_transfer_.received != Bl_transfer_.expected) {
                return _SM_HANDLED();
            }
            if (Bl_acceptBlock_(Bl_transfer_.index, Bl_transfer_.expected,
                                 Bl_transfer_.crc16)) {
                Bl_ack1_();
            }
            else { Bl_nak_(); }
            return _SM_TRAN(&Bl_control);
        }
        case BL_GAP_SIG: {
            ++Bl_retry_;
            Bl_transfer_.nakPending = true;
            return _SM_TRAN(&Bl_resync);
        }
        default: { return _SM_SUPER(); }
    }
}
static SM_RetState Bl_resync_(SM_Hsm *me, Bl_TransferEvt const *e) {
    switch (e->sig) {
        case BL_BYTE_SIG: { return _SM_HANDLED(); }
        case BL_GAP_SIG: {
            if (Bl_transfer_.nakPending) { Bl_nak_(); }
            Bl_transfer_.nakPending = false;
            return _SM_TRAN(&Bl_control);
        }
        default: { return _SM_SUPER(); }
    }
}

// Boot stages own command routing; transfer framing remains a separate HSM.
static struct {
    SM_Hsm hsm;
    HdlcParser parser;
    uint32_t startedAt, lastByte;
    uint32_t receiveTimeout;
    uint8_t command[ASSET_LINK_CONTROL_MAX], commandSize;
    bool frameReady, commandPending;
} Bl_boot_;

static SM_RetState Bl_bootHandshake_(SM_Hsm *me, Bl_TransferEvt const *e);
static SM_RetState Bl_bootAssets_(SM_Hsm *me, Bl_TransferEvt const *e);
static SM_RetState Bl_bootStopped_(SM_Hsm *me, Bl_TransferEvt const *e);
static void Bl_bootHandshakeEntry_(SM_Hsm *me);
static void Bl_bootAssetsEntry_(SM_Hsm *me);
static void Bl_bootAppEntry_(SM_Hsm *me);
static void Bl_bootFaultEntry_(SM_Hsm *me);
static void Bl_bootInvalidEntry_(SM_Hsm *me);
static SM_HsmState const Bl_bootHandshake = {
    SM_HSM_TOP, NULL, (SM_ActionHandler)Bl_bootHandshakeEntry_, NULL,
    (SM_StateHandler)Bl_bootHandshake_
};
static SM_HsmState const Bl_bootAssets = {
    SM_HSM_TOP, NULL, (SM_ActionHandler)Bl_bootAssetsEntry_, NULL,
    (SM_StateHandler)Bl_bootAssets_
};
static SM_HsmState const Bl_bootApp = {
    SM_HSM_TOP, NULL, (SM_ActionHandler)Bl_bootAppEntry_, NULL, NULL
};
static SM_HsmState const Bl_bootFault = {
    SM_HSM_TOP, NULL, (SM_ActionHandler)Bl_bootFaultEntry_, NULL,
    (SM_StateHandler)Bl_bootStopped_
};
static SM_HsmState const Bl_bootInvalidApp = {
    SM_HSM_TOP, NULL, (SM_ActionHandler)Bl_bootInvalidEntry_, NULL,
    (SM_StateHandler)Bl_bootStopped_
};

static SM_StatePtr Bl_bootInitial_(SM_Hsm *me) {
    (void)me;
    Bl_boot_.receiveTimeout = 20U;
    if (Bl_faultBoot(Bl_uart_)) { return _SM_INIT(&Bl_bootFault); }
    if (_snoinit == BL_FLAG_MAGIC) { return _SM_INIT(&Bl_bootAssets); }
    return _SM_INIT(&Bl_bootHandshake);
}

// The parser reports syntax only. Stage handlers validate command semantics.
static void Bl_bootCommand_(void *context, uint8_t const *frame,
                             size_t size) {
    (void)context;
    if (frame[1] == ASSET_LINK_REQUEST && size > 3U
        && size <= 3U + sizeof(Bl_boot_.command)) {
        Bl_boot_.commandSize = (uint8_t)(size - 3U);
        memcpy(Bl_boot_.command, frame + 3U, size - 3U);
        Bl_boot_.frameReady = true;
    }
}

static void Bl_bootHandshakeEntry_(SM_Hsm *me) {
    (void)me;
    Bl_boot_.receiveTimeout = 1U;
    Bl_boot_.startedAt = Bl_boot_.lastByte = HAL_GetTick();
    Bl_boot_.frameReady = Bl_boot_.commandPending = false;
    HdlcParser_ctor(&Bl_boot_.parser, &Bl_bootCommand_, NULL);
    HdlcParser_init(&Bl_boot_.parser);
}

static SM_RetState Bl_bootHandshake_(SM_Hsm *me, Bl_TransferEvt const *e) {
    uint32_t const now = HAL_GetTick();
    if ((uint32_t)(now - Bl_boot_.startedAt) >= BL_BOOT_MS) {
        if (Bl_appSane_()) { return _SM_TRAN(&Bl_bootApp); }
        return _SM_TRAN(&Bl_bootInvalidApp);
    }
    switch (e->sig) {
        case BL_BYTE_SIG:
        case BL_GAP_SIG: {
            if ((uint32_t)(now - Bl_boot_.lastByte) >= 20U) {
                HdlcParser_init(&Bl_boot_.parser);
                Bl_boot_.frameReady = false;
            }
            if (e->sig == BL_GAP_SIG) { return _SM_HANDLED(); }
            Bl_boot_.lastByte = now;
            // Closing flag commits a command; dispatch after this returns.
            if (e->byte == 0x7EU && Bl_boot_.frameReady) {
                Bl_boot_.commandPending = true;
            }
            Bl_boot_.frameReady = false;
            HdlcParser_input(&Bl_boot_.parser, e->byte);
            return _SM_HANDLED();
        }
        case BL_BOOT_COMMAND_SIG: {
            uint8_t const *command = Bl_boot_.command;
            switch (command[0]) {
                case BL_REQ_MAGIC:
                    if (Bl_boot_.commandSize != BL_REQ_LEN
                        || !Bl_frameOk_(command)) {
                        return _SM_HANDLED();
                    }
                    Bl_len_ = Bl_be32_(command + 2U);
                    Bl_requestKind_ = BL_REQ_MAGIC;
                    Bl_requestIndex_ = 0U;
                    ++Bl_frames_;
                    ++Bl_ok_;
                    return _SM_TRAN(&Bl_bootAssets);
                default:
                    // Future IAP/OTA commands get explicit stage routes here.
                    return _SM_HANDLED();
            }
        }
        default: { return _SM_SUPER(); }
    }
}

static void Bl_bootAssetsEntry_(SM_Hsm *me) {
    (void)me;
    Bl_boot_.receiveTimeout = 20U;
    Bl_say_("[bootloader] burn mode: HDLC commands, raw asset data\r\n");
    BSP_flashInit();
    Bl_flashBindWait_();
    HdlcParser_ctor(&Bl_transfer_.parser, &Bl_controlFrame_, NULL);
    SM_Hsm_init_(&Bl_transfer_.hsm, (SM_InitHandler)Bl_transferInitial_);
    if (Bl_len_ != 0U) { Bl_ackFrame_(); }
}

static SM_RetState Bl_bootAssets_(SM_Hsm *me, Bl_TransferEvt const *e) {
    (void)me;
    switch (e->sig) {
        case BL_BYTE_SIG:
        case BL_GAP_SIG: {
            SM_Hsm_dispatch_(&Bl_transfer_.hsm, e);
            if (Bl_transfer_.pending) {
                Bl_transfer_.pending = false;
                Bl_TransferEvt const command = {
                    .sig = Bl_transfer_.command
                };
                SM_Hsm_dispatch_(&Bl_transfer_.hsm, &command);
            }
            return _SM_HANDLED();
        }
        default: { return _SM_SUPER(); }
    }
}

static void Bl_bootAppEntry_(SM_Hsm *me) {
    (void)me;
    Bl_say_("[bootloader] handing over to the app\r\n");
    Bl_handOver_();
}

static void Bl_bootFaultEntry_(SM_Hsm *me) {
    (void)me;
    Bl_say_("[bootloader] retained fault: staying put\r\n");
    // Future OTA rollback trigger for a valid no-init fault snapshot.
}

static void Bl_bootInvalidEntry_(SM_Hsm *me) {
    (void)me;
    Bl_say_("[bootloader] app vector table looks wrong: staying put\r\n");
}

static SM_RetState Bl_bootStopped_(SM_Hsm *me, Bl_TransferEvt const *e) {
    (void)me;
    (void)e;
    return _SM_HANDLED();
}

static void Bl_bootDispatch_(Bl_TransferEvt const *e) {
    SM_Hsm_dispatch_(&Bl_boot_.hsm, e);
    if (Bl_boot_.commandPending) {
        Bl_boot_.commandPending = false;
        Bl_TransferEvt const command = {.sig = BL_BOOT_COMMAND_SIG};
        SM_Hsm_dispatch_(&Bl_boot_.hsm, &command);
    }
}

void Bl_Boot_run(UART_HandleTypeDef *uart) {
    Bl_uart_ = uart;
    Bl_say_("\r\n[bootloader] up\r\n");
    SM_Hsm_init_(&Bl_boot_.hsm, (SM_InitHandler)Bl_bootInitial_);
    for (;;) {
        Bl_TransferEvt e = {.sig = BL_BYTE_SIG};
        if (HAL_UART_Receive(Bl_uart_, &e.byte, 1U,
                            Bl_boot_.receiveTimeout) != HAL_OK) {
            e.sig = BL_GAP_SIG;
        }
        Bl_bootDispatch_(&e);
    }
}
//............................................................................
// Cheap sanity check on the image we are about to hand the CPU to. Both words
// matter: a stack pointer outside RAM, or a reset vector with the Thumb bit
// clear, faults on the first instruction fetch, and a debugger is then the
// only way back in.
static bool Bl_appSane_(void) {
    uint32_t const sp = *(uint32_t const *)BL_APP_BASE;
    uint32_t const pc = *(uint32_t const *)(BL_APP_BASE + 4U);

    if ((sp < BL_RAM_BASE) || (sp > BL_RAM_END) || ((sp & 0x3U) != 0U)) {
        return false;
    }
    if ((pc < BL_APP_BASE) || (pc >= BL_FLASH_END) || ((pc & 0x1U) == 0U)) {
        return false;
    }

    return true;
}
//............................................................................
static void Bl_handOver_(void) {
    uint32_t const sp = *(uint32_t const *)BL_APP_BASE;
    uint32_t const pc = *(uint32_t const *)(BL_APP_BASE + 4U);
    uint8_t i;

    __disable_irq();

    // Give the peripherals back the way the app expects to find them at a
    // cold
    // start: this program opened UART1 and the TIM1 time base, and left no
    // IRQ
    // pending. HAL_DeInit() also takes the SysTick clock off `uwTick`, so the
    // app starts its own time base from zero.
    HAL_UART_DeInit(Bl_uart_);
    HAL_DeInit();

    for (i = 0U; i < 8U; ++i) {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    SCB->VTOR = BL_APP_BASE;
    __set_MSP(sp);
    __enable_irq();
    ((void (*)(void))pc)();

    for (;;) {      // the app never returns
    }
}
