//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "hdlc_parser.h"

typedef struct {
    uint8_t frame[HDLC_PARSER_FRAME_MAX_SIZE];
    size_t size;
    unsigned count;
} FrameSink;

static void captureFrame_(void * const ctx,
                          uint8_t const * const frame,
                          size_t const size)
{
    FrameSink * const sink = (FrameSink *)ctx;
    memcpy(sink->frame, frame, size);
    sink->size = size;
    ++sink->count;
}

static uint8_t checksum_(uint8_t const * const frame,
                         size_t const size)
{
    uint8_t sum = 0U;
    for (size_t i = 0U; i < size; ++i) {
        sum = (uint8_t)(sum + frame[i]);
    }
    return (uint8_t)(0xFFU - sum);
}

static void feed_(HdlcParser * const parser,
                  uint8_t const * const data,
                  size_t const size)
{
    for (size_t i = 0U; i < size; ++i) {
        HdlcParser_input(parser, data[i]);
    }
}

int main(void) {
    int failed = 0;
    FrameSink sink = {0};
    HdlcParser parser;
    HdlcParser_ctor(&parser, &captureFrame_, &sink);
    HdlcParser_init(&parser);

    uint8_t const plainFrame[] = {0x01U, 0x02U, 0x02U, 0x10U, 0x20U};
    uint8_t plainWire[sizeof(plainFrame) + 2U] = {0x7EU};
    memcpy(&plainWire[1], plainFrame, sizeof(plainFrame));
    plainWire[sizeof(plainWire) - 1U] =
        checksum_(plainFrame, sizeof(plainFrame));
    feed_(&parser, plainWire, sizeof(plainWire));
    failed += (sink.count == 1U)
              && (sink.size == sizeof(plainFrame))
              && (memcmp(sink.frame, plainFrame,
                         sizeof(plainFrame)) == 0)
              ? 0 : 1;

    uint8_t const escapedFrame[] = {
        0x03U, 0x04U, 0x02U, 0x7EU, 0x7DU,
    };
    uint8_t const escapedWire[] = {
        0x7EU, 0x03U, 0x04U, 0x02U,
        0x7DU, 0x5EU, 0x7DU, 0x5DU, 0xFBU,
    };
    feed_(&parser, escapedWire, sizeof(escapedWire));
    failed += (sink.count == 2U)
              && (sink.size == sizeof(escapedFrame))
              && (memcmp(sink.frame, escapedFrame,
                         sizeof(escapedFrame)) == 0)
              ? 0 : 1;

    uint8_t escapedLengthFrame[3U + 0x7EU] = {
        0x01U, 0x04U, 0x7EU,
    };
    uint8_t escapedLengthWire[sizeof(escapedLengthFrame) + 3U] = {
        0x7EU, 0x01U, 0x04U, 0x7DU, 0x5EU,
    };
    escapedLengthWire[sizeof(escapedLengthWire) - 1U] =
        checksum_(escapedLengthFrame, sizeof(escapedLengthFrame));
    feed_(&parser, escapedLengthWire, sizeof(escapedLengthWire));
    failed += (sink.count == 3U)
              && (sink.size == sizeof(escapedLengthFrame))
              && (memcmp(sink.frame, escapedLengthFrame,
                         sizeof(escapedLengthFrame)) == 0)
              ? 0 : 1;

    uint8_t const escapedChecksumWire[] = {
        0x7EU, 0x01U, 0x80U, 0x00U, 0x7DU, 0x5EU,
    };
    feed_(&parser, escapedChecksumWire,
          sizeof(escapedChecksumWire));
    uint8_t const escapedChecksumFrame[] = {
        0x01U, 0x80U, 0x00U,
    };
    failed += (sink.count == 4U)
              && (sink.size == sizeof(escapedChecksumFrame))
              && (memcmp(sink.frame, escapedChecksumFrame,
                         sizeof(escapedChecksumFrame)) == 0)
              ? 0 : 1;

    uint8_t const badChecksum[] = {
        0x7EU, 0x05U, 0x06U, 0x00U, 0x00U,
    };
    feed_(&parser, badChecksum, sizeof(badChecksum));
    failed += sink.count == 4U ? 0 : 1;

    uint8_t const resync[] = {
        0x7EU, 0x11U, 0x22U,
        0x7EU, 0x07U, 0x08U, 0x00U, 0xF0U,
    };
    feed_(&parser, resync, sizeof(resync));
    uint8_t const resyncedFrame[] = {0x07U, 0x08U, 0x00U};
    failed += (sink.count == 5U)
              && (sink.size == sizeof(resyncedFrame))
              && (memcmp(sink.frame, resyncedFrame,
                         sizeof(resyncedFrame)) == 0)
              ? 0 : 1;

    return failed == 0 ? 0 : 1;
}
