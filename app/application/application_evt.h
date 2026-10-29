//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef APPLICATION_EVT_H_
#define APPLICATION_EVT_H_

// Common signals occupy [1, APPLICATION_PUB_SIG_MAX). Both SST AOs and
// ordinary-thread HSMs may consume them. Every private enum starts at MAX;
// only common signals belong in the SST publication/subscription table.
// READY: the splash load attempt has finished; UI may show its fallback.
// ASSETS_LOADED: asset loading/validation is finished, before NVS restore.
enum ApplicationSignals {
    BRING_UP_READY_SIG = 1U,
    BRING_UP_ASSETS_LOADED_SIG,
    // Initialization finished, including failure; not a usability promise.
    BRING_UP_NVM_READY_SIG,
    APPLICATION_PUB_SIG_MAX
};

#endif // APPLICATION_EVT_H_
