//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef BSP_H_
#define BSP_H_

//============================================================================
#include "sm_port.h"

//============================================================================
#define BSP_TICKS_PER_SEC 100U

//============================================================================
void BSP_init(void);
void BSP_ledOn(void);
void BSP_ledOff(void);

#endif // BSP_H_
