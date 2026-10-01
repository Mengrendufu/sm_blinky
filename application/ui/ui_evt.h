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

#include <stdbool.h>

//============================================================================
//=== UI event ingress -- thread safe, callable from any AO
//
// Events are values, not pool blocks: nothing is allocated, nothing blocks. A
// full inbox drops the event and returns false, so a producer is never held
// up by the UI thread. A successful post also wakes the thread, so an event
// never has to wait for the next poll timeout.

bool Ui_postLedOn(void);
bool Ui_postLedOff(void);

#endif // UI_EVT_H_
