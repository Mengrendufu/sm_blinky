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

// Cumulative snapshot: UI adds this boot's deltas; Flash adds the restored
// base once and checkpoints the total. Addition preserves either ordering.
// Every operation uses a short critical section; NOR access stays in Flash.
void Nvm_addBlinky(uint32_t edges);
uint32_t Nvm_blinky(void);

#endif /* NVM_H_ */
