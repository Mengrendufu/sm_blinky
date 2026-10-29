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
//=== Component: HdlcParser
#include <stdbool.h>
#include "sm_assert.h"
#include "hdlc_parser.h"
SM_DEFINE_MODULE("hdlc_parser")

enum HdlcParserSignals_ {
    HDLC_PARSER_NULL_SIG_,
    HDLC_PARSER_FRAME_FLAG_SIG_,
    HDLC_PARSER_ESCAPE_SIG_,
    HDLC_PARSER_BYTE_SIG_,
};

typedef struct {
    uint8_t sig;
    uint8_t byte;
} HdlcParserEvt_;

//============================================================================
//=== HSM states

// TOP-INIT
static SM_StatePtr HdlcParser_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT;

// idle
static SM_RetState HdlcParser_idle_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT;
static SM_HsmState SM_HSM_ROM HdlcParser_idle = {
    SM_HSM_TOP,                           // super
    (SM_InitHandler)0,                    // init_
    (SM_ActionHandler)0,                  // entry_
    (SM_ActionHandler)0,                  // exit_
    (SM_StateHandler)&HdlcParser_idle_    // handler
};

// active
static SM_StatePtr HdlcParser_active_init_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState HdlcParser_active_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT;
static SM_HsmState SM_HSM_ROM HdlcParser_active = {
    SM_HSM_TOP,                           // super
    (SM_InitHandler)&HdlcParser_active_init_, // init_
    (SM_ActionHandler)0,                  // entry_
    (SM_ActionHandler)0,                  // exit_
    (SM_StateHandler)&HdlcParser_active_  // handler
};

// waitSeq
static SM_RetState HdlcParser_waitSeq_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT;
static SM_HsmState SM_HSM_ROM HdlcParser_waitSeq = {
    (SM_StatePtr)&HdlcParser_active,      // super
    (SM_InitHandler)0,                    // init_
    (SM_ActionHandler)0,                  // entry_
    (SM_ActionHandler)0,                  // exit_
    (SM_StateHandler)&HdlcParser_waitSeq_ // handler
};

// waitSeqEscaped
static SM_RetState HdlcParser_waitSeqEscaped_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT;
static SM_HsmState SM_HSM_ROM HdlcParser_waitSeqEscaped = {
    (SM_StatePtr)&HdlcParser_waitSeq,             // super
    (SM_InitHandler)0,                            // init_
    (SM_ActionHandler)0,                          // entry_
    (SM_ActionHandler)0,                          // exit_
    (SM_StateHandler)&HdlcParser_waitSeqEscaped_  // handler
};

// waitRecId
static SM_RetState HdlcParser_waitRecId_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT;
static SM_HsmState SM_HSM_ROM HdlcParser_waitRecId = {
    (SM_StatePtr)&HdlcParser_active,        // super
    (SM_InitHandler)0,                      // init_
    (SM_ActionHandler)0,                    // entry_
    (SM_ActionHandler)0,                    // exit_
    (SM_StateHandler)&HdlcParser_waitRecId_ // handler
};

// waitRecIdEscaped
static SM_RetState HdlcParser_waitRecIdEscaped_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT;
static SM_HsmState SM_HSM_ROM HdlcParser_waitRecIdEscaped = {
    (SM_StatePtr)&HdlcParser_waitRecId,            // super
    (SM_InitHandler)0,                             // init_
    (SM_ActionHandler)0,                           // entry_
    (SM_ActionHandler)0,                           // exit_
    (SM_StateHandler)&HdlcParser_waitRecIdEscaped_ // handler
};

// waitLength
static SM_RetState HdlcParser_waitLength_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT;
static SM_HsmState SM_HSM_ROM HdlcParser_waitLength = {
    (SM_StatePtr)&HdlcParser_active,        // super
    (SM_InitHandler)0,                      // init_
    (SM_ActionHandler)0,                    // entry_
    (SM_ActionHandler)0,                    // exit_
    (SM_StateHandler)&HdlcParser_waitLength_ // handler
};

// waitLengthEscaped
static SM_RetState HdlcParser_waitLengthEscaped_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT;
static SM_HsmState SM_HSM_ROM HdlcParser_waitLengthEscaped = {
    (SM_StatePtr)&HdlcParser_waitLength,            // super
    (SM_InitHandler)0,                              // init_
    (SM_ActionHandler)0,                            // entry_
    (SM_ActionHandler)0,                            // exit_
    (SM_StateHandler)&HdlcParser_waitLengthEscaped_ // handler
};

// waitPayload
static SM_RetState HdlcParser_waitPayload_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT;
static SM_HsmState SM_HSM_ROM HdlcParser_waitPayload = {
    (SM_StatePtr)&HdlcParser_active,         // super
    (SM_InitHandler)0,                       // init_
    (SM_ActionHandler)0,                     // entry_
    (SM_ActionHandler)0,                     // exit_
    (SM_StateHandler)&HdlcParser_waitPayload_ // handler
};

// waitPayloadEscaped
static SM_RetState HdlcParser_waitPayloadEscaped_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT;
static SM_HsmState SM_HSM_ROM HdlcParser_waitPayloadEscaped = {
    (SM_StatePtr)&HdlcParser_waitPayload,            // super
    (SM_InitHandler)0,                               // init_
    (SM_ActionHandler)0,                             // entry_
    (SM_ActionHandler)0,                             // exit_
    (SM_StateHandler)&HdlcParser_waitPayloadEscaped_ // handler
};

// waitChecksum
static SM_RetState HdlcParser_waitChecksum_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT;
static SM_HsmState SM_HSM_ROM HdlcParser_waitChecksum = {
    (SM_StatePtr)&HdlcParser_active,          // super
    (SM_InitHandler)0,                        // init_
    (SM_ActionHandler)0,                      // entry_
    (SM_ActionHandler)0,                      // exit_
    (SM_StateHandler)&HdlcParser_waitChecksum_ // handler
};

// waitChecksumEscaped
static SM_RetState HdlcParser_waitChecksumEscaped_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT;
static SM_HsmState SM_HSM_ROM HdlcParser_waitChecksumEscaped = {
    (SM_StatePtr)&HdlcParser_waitChecksum,            // super
    (SM_InitHandler)0,                                // init_
    (SM_ActionHandler)0,                              // entry_
    (SM_ActionHandler)0,                              // exit_
    (SM_StateHandler)&HdlcParser_waitChecksumEscaped_ // handler
};

//============================================================================
//=== Frame context

static void HdlcParser_reset_(HdlcParser * const me) {
    me->frameSize = 0U;
    me->payloadRemaining = 0U;
    me->checksum = 0U;
}

static void HdlcParser_append_(HdlcParser * const me,
                               uint8_t const byte)
{
    SM_ASSERT(me->frameSize < sizeof(me->frame));
    me->frame[me->frameSize++] = byte;
    me->checksum = (uint8_t)(me->checksum + byte);
}

static uint8_t HdlcParser_unescape_(uint8_t const byte) {
    return (uint8_t)(byte ^ 0x20U);
}

static bool HdlcParser_checksumValid_(HdlcParser const * const me,
                                      uint8_t const checksum)
{
    return (uint8_t)(me->checksum + checksum) == 0xFFU;
}

static void HdlcParser_emitIfValid_(HdlcParser * const me,
                                    uint8_t const checksum)
{
    if (HdlcParser_checksumValid_(me, checksum)) {
        (*me->sink)(me->sinkCtx, me->frame, me->frameSize);
    }
}

//============================================================================
//=== HSM implementations

// TOP-INIT
static SM_StatePtr HdlcParser_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;
    return _SM_INIT(&HdlcParser_idle);
}

// idle
static SM_RetState HdlcParser_idle_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT
{
    (void)me;
    switch (e->sig) {
        case HDLC_PARSER_FRAME_FLAG_SIG_: {
            return _SM_TRAN(&HdlcParser_active);
        }
        default: {
            return _SM_SUPER();
        }
    }
}

// active
static SM_StatePtr HdlcParser_active_init_(SM_Hsm * const me) SM_HSM_RETT {
    HdlcParser_reset_((HdlcParser *)(void *)me);
    return _SM_INIT(&HdlcParser_waitSeq);
}

static SM_RetState HdlcParser_active_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT
{
    (void)me;
    switch (e->sig) {
        case HDLC_PARSER_FRAME_FLAG_SIG_: {
            return _SM_TRAN(&HdlcParser_active);
        }
        default: {
            return _SM_SUPER();
        }
    }
}

// waitSeq
static SM_RetState HdlcParser_waitSeq_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT
{
    HdlcParser * const parser = (HdlcParser *)(void *)me;
    switch (e->sig) {
        case HDLC_PARSER_ESCAPE_SIG_: {
            return _SM_TRAN(&HdlcParser_waitSeqEscaped);
        }
        case HDLC_PARSER_BYTE_SIG_: {
            HdlcParser_append_(parser, e->byte);
            return _SM_TRAN(&HdlcParser_waitRecId);
        }
        default: {
            return _SM_SUPER();
        }
    }
}

// waitSeqEscaped
static SM_RetState HdlcParser_waitSeqEscaped_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT
{
    HdlcParser * const parser = (HdlcParser *)(void *)me;
    switch (e->sig) {
        case HDLC_PARSER_ESCAPE_SIG_: {
            return _SM_TRAN(&HdlcParser_idle);
        }
        case HDLC_PARSER_BYTE_SIG_: {
            HdlcParser_append_(parser, HdlcParser_unescape_(e->byte));
            return _SM_TRAN(&HdlcParser_waitRecId);
        }
        default: {
            return _SM_SUPER();
        }
    }
}

// waitRecId
static SM_RetState HdlcParser_waitRecId_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT
{
    HdlcParser * const parser = (HdlcParser *)(void *)me;
    switch (e->sig) {
        case HDLC_PARSER_ESCAPE_SIG_: {
            return _SM_TRAN(&HdlcParser_waitRecIdEscaped);
        }
        case HDLC_PARSER_BYTE_SIG_: {
            HdlcParser_append_(parser, e->byte);
            return _SM_TRAN(&HdlcParser_waitLength);
        }
        default: {
            return _SM_SUPER();
        }
    }
}

// waitRecIdEscaped
static SM_RetState HdlcParser_waitRecIdEscaped_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT
{
    HdlcParser * const parser = (HdlcParser *)(void *)me;
    switch (e->sig) {
        case HDLC_PARSER_ESCAPE_SIG_: {
            return _SM_TRAN(&HdlcParser_idle);
        }
        case HDLC_PARSER_BYTE_SIG_: {
            HdlcParser_append_(parser, HdlcParser_unescape_(e->byte));
            return _SM_TRAN(&HdlcParser_waitLength);
        }
        default: {
            return _SM_SUPER();
        }
    }
}

// waitLength
static SM_RetState HdlcParser_waitLength_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT
{
    HdlcParser * const parser = (HdlcParser *)(void *)me;
    switch (e->sig) {
        case HDLC_PARSER_ESCAPE_SIG_: {
            return _SM_TRAN(&HdlcParser_waitLengthEscaped);
        }
        case HDLC_PARSER_BYTE_SIG_: {
            HdlcParser_append_(parser, e->byte);
            parser->payloadRemaining = e->byte;
            if (parser->payloadRemaining == 0U) {
                return _SM_TRAN(&HdlcParser_waitChecksum);
            } else {
                return _SM_TRAN(&HdlcParser_waitPayload);
            }
        }
        default: {
            return _SM_SUPER();
        }
    }
}

// waitLengthEscaped
static SM_RetState HdlcParser_waitLengthEscaped_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT
{
    HdlcParser * const parser = (HdlcParser *)(void *)me;
    switch (e->sig) {
        case HDLC_PARSER_ESCAPE_SIG_: {
            return _SM_TRAN(&HdlcParser_idle);
        }
        case HDLC_PARSER_BYTE_SIG_: {
            uint8_t const length = HdlcParser_unescape_(e->byte);
            HdlcParser_append_(parser, length);
            parser->payloadRemaining = length;
            if (parser->payloadRemaining == 0U) {
                return _SM_TRAN(&HdlcParser_waitChecksum);
            } else {
                return _SM_TRAN(&HdlcParser_waitPayload);
            }
        }
        default: {
            return _SM_SUPER();
        }
    }
}

// waitPayload
static SM_RetState HdlcParser_waitPayload_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT
{
    HdlcParser * const parser = (HdlcParser *)(void *)me;
    switch (e->sig) {
        case HDLC_PARSER_ESCAPE_SIG_: {
            return _SM_TRAN(&HdlcParser_waitPayloadEscaped);
        }
        case HDLC_PARSER_BYTE_SIG_: {
            SM_ASSERT(parser->payloadRemaining > 0U);
            HdlcParser_append_(parser, e->byte);
            --parser->payloadRemaining;
            if (parser->payloadRemaining == 0U) {
                return _SM_TRAN(&HdlcParser_waitChecksum);
            } else {
                return _SM_HANDLED();
            }
        }
        default: {
            return _SM_SUPER();
        }
    }
}

// waitPayloadEscaped
static SM_RetState HdlcParser_waitPayloadEscaped_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT
{
    HdlcParser * const parser = (HdlcParser *)(void *)me;
    switch (e->sig) {
        case HDLC_PARSER_ESCAPE_SIG_: {
            return _SM_TRAN(&HdlcParser_idle);
        }
        case HDLC_PARSER_BYTE_SIG_: {
            SM_ASSERT(parser->payloadRemaining > 0U);
            HdlcParser_append_(parser, HdlcParser_unescape_(e->byte));
            --parser->payloadRemaining;
            if (parser->payloadRemaining == 0U) {
                return _SM_TRAN(&HdlcParser_waitChecksum);
            } else {
                return _SM_TRAN(&HdlcParser_waitPayload);
            }
        }
        default: {
            return _SM_SUPER();
        }
    }
}

// waitChecksum
static SM_RetState HdlcParser_waitChecksum_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT
{
    HdlcParser * const parser = (HdlcParser *)(void *)me;
    switch (e->sig) {
        case HDLC_PARSER_ESCAPE_SIG_: {
            return _SM_TRAN(&HdlcParser_waitChecksumEscaped);
        }
        case HDLC_PARSER_BYTE_SIG_: {
            HdlcParser_emitIfValid_(parser, e->byte);
            return _SM_TRAN(&HdlcParser_active);
        }
        default: {
            return _SM_SUPER();
        }
    }
}

// waitChecksumEscaped
static SM_RetState HdlcParser_waitChecksumEscaped_(
    SM_Hsm * const me, HdlcParserEvt_ const * const e) SM_HSM_RETT
{
    HdlcParser * const parser = (HdlcParser *)(void *)me;
    switch (e->sig) {
        case HDLC_PARSER_ESCAPE_SIG_: {
            return _SM_TRAN(&HdlcParser_idle);
        }
        case HDLC_PARSER_BYTE_SIG_: {
            HdlcParser_emitIfValid_(parser,
                                    HdlcParser_unescape_(e->byte));
            return _SM_TRAN(&HdlcParser_active);
        }
        default: {
            return _SM_SUPER();
        }
    }
}

//============================================================================
//=== Lifecycle and input

void HdlcParser_ctor(HdlcParser * const me,
                     HdlcParserFrameSink const sink,
                     void * const sinkCtx)
{
    SM_REQUIRE(me != (HdlcParser *)0);
    SM_REQUIRE(sink != (HdlcParserFrameSink)0);

    me->super.curr = (SM_StatePtr)0;
    me->super.next = (SM_StatePtr)0;
    HdlcParser_reset_(me);
    me->sink = sink;
    me->sinkCtx = sinkCtx;
}

void HdlcParser_init(HdlcParser * const me) {
    SM_REQUIRE(me != (HdlcParser *)0);
    SM_Hsm_init_(&me->super,
                 (SM_InitHandler)HdlcParser_TOP_initial_);
}

void HdlcParser_input(HdlcParser * const me,
                      uint8_t const byte)
{
    SM_REQUIRE(me != (HdlcParser *)0);

    HdlcParserEvt_ const e = {
        .sig = byte == 0x7EU
               ? HDLC_PARSER_FRAME_FLAG_SIG_
               : (byte == 0x7DU
                  ? HDLC_PARSER_ESCAPE_SIG_
                  : HDLC_PARSER_BYTE_SIG_),
        .byte = byte,
    };
    SM_Hsm_dispatch_(&me->super, &e);
}
