//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "ui.h"
#include "ui_evt.h"
#include "sm_ui_evt.h"

// Fixed BSP timebase: one invocation, one timeout event. No runtime arming.
void Ui_tick(void *context) {
    static SST_Evt const evt = {
        .sig = UI_TIMEOUT_SIG, .poolId = 0U, .refCtr = 0U
    };

    (void)context;
    Ui_post(&evt);
}
