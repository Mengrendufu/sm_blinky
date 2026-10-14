//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software; it comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
//============================================================================
#ifndef NVM_H_
#define NVM_H_

#include <stdint.h>

// The application's snapshot of what the NVM holds. Two threads meet here -
// the UI publishes counts, the flash task reads them out every ten seconds -
// so both ends go through these two calls, each a short critical section: a
// u32 load or store, nothing else. The store itself (mount, read, write, the
// NOR) is the flash task's business and never leaks out of it.
void Nvm_setBlinky(uint32_t value);
uint32_t Nvm_blinky(void);

#endif /* NVM_H_ */
