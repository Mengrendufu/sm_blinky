//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef UI_EVT_H_
#define UI_EVT_H_

#include "sst.h"

//============================================================================
//=== UI event ingress -- thread safe, callable from any AO
//
// Task context only, after Ui_start binds the inbox. Posting never blocks.
// The inbox must have capacity: overflow asserts, following SST semantics.
// Posting acquires a queue reference; the UI calls SST_GC after dispatch.
// Do not access an event after handing off its last reference. Static events
// (poolId=0) are immutable and must outlive every queued reference; current
// signal wrappers use static storage.
void Ui_post(SST_Evt const *evt);

void Ui_postLedOn(void);
void Ui_postLedOff(void);

#endif // UI_EVT_H_
