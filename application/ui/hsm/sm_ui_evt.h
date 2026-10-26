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
#include "application_evt.h"

// UI signals use the standard SST_Evt header. Static events have poolId=0;
// payload events embed SST_Evt as their first member.

enum {
    UI_NULL_SIG = 0,
    UI_LED_ON_SIG = APPLICATION_PUB_SIG_MAX,
    UI_LED_OFF_SIG,
    UI_GO_SPLASHING_SIG,
    // Shared by mutually exclusive Splash/Sys/Live deadline owners.
    UI_STATE_TIMEOUT_SIG,
    // Posted after Active updates shared data; pages render that data.
    UI_REFRESH_SIG,
    UI_HEADER_TIMEOUT_SIG,
    UI_DIAL_TIMEOUT_SIG
};

#endif // SM_UI_EVT_H_
