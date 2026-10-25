//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef APPLICATION_H_
#define APPLICATION_H_

//============================================================================
// The boot phase's records, including the timeline the tracer cannot push
// itself: call once, from the Flash first bring-up event.
void Application_dbgBootReport(void);

// Mount tick clients after BSP bring-up, before scheduler startup.
void Application_tickStart(void);

void AO_start(void);

//============================================================================
#include "aos/blinky/blinky.h"

#endif // APPLICATION_H_
