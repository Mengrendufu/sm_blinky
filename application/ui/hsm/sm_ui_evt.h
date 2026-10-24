//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef SM_UI_EVT_H_
#define SM_UI_EVT_H_

#include "sst.h"

// UI signals use the standard SST_Evt header. Static events have poolId=0;
// payload events embed SST_Evt as their first member.

enum {
    UI_NULL_SIG = 0,
    UI_LED_ON_SIG,
    UI_LED_OFF_SIG,
    // Fixed-frequency UI timebase: states count pulses for transition
    // guards and update their controls on the same event.
    UI_TIMEOUT_SIG
};

#endif // SM_UI_EVT_H_
