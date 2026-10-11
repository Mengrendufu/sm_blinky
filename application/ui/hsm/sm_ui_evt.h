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

#include <stdint.h>

//============================================================================
//=== Event contract shared by the UI inbox and the UI HSM

typedef uint16_t Ui_Sig;

typedef struct {
    Ui_Sig sig;
} Ui_Evt;

enum {
    UI_NULL_SIG = 0,
    UI_LED_ON_SIG,
    UI_LED_OFF_SIG,
    // Delivered by the UI thread, not by the inbox: the splash page's hold is
    // over, so the page that owns the glass changes. It is the thread's clock
    // that raises it, and the state machine's decision that acts on it.
    UI_SPLASH_DONE_SIG
};

#endif // SM_UI_EVT_H_
