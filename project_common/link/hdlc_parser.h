//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef HDLC_PARSER_H_
#define HDLC_PARSER_H_

#include <stddef.h>
#include <stdint.h>
#include "sm_hsm.h"

#define HDLC_PARSER_FRAME_MAX_SIZE (3U + UINT8_MAX)

typedef void (*HdlcParserFrameSink)(void *ctx,
                                    uint8_t const *frame,
                                    size_t size);

typedef struct {
    SM_Hsm super;
    uint8_t frame[HDLC_PARSER_FRAME_MAX_SIZE];
    size_t frameSize;
    uint8_t payloadRemaining;
    uint8_t checksum;
    HdlcParserFrameSink sink;
    void *sinkCtx;
} HdlcParser;

void HdlcParser_ctor(HdlcParser *me,
                     HdlcParserFrameSink sink,
                     void *sinkCtx);
void HdlcParser_init(HdlcParser *me);
void HdlcParser_input(HdlcParser *me, uint8_t byte);

#endif // HDLC_PARSER_H_
