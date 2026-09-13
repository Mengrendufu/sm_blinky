//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef BLINKY_H_
#define BLINKY_H_

//============================================================================
enum BlinkySignals {
    BLINKY_TIMEOUT_SIG = 1U,
    BLINKYSIGNALS_MAX
};

//============================================================================
extern SST_Task * const AO_Blinky;

//============================================================================
void Blinky_ctor(void);

#endif // BLINKY_H_
