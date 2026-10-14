//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software; it comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
//============================================================================
#ifndef W_NVM_H_
#define W_NVM_H_

#include <stdint.h>

struct _lv_obj_t;

// The one readout that comes out of the NOR by itself: a counter that keeps
// counting across a power cycle. The label is as wide as the value can get
// (twenty one characters at the default font is 163 px) and clips rather than
// wraps, so it can never push the row below it around.
struct W_Nvm {
    struct _lv_obj_t *label;
};

void W_Nvm_init(struct W_Nvm *w);
void W_Nvm_create(struct W_Nvm *w, struct _lv_obj_t *parent,
                  int32_t x, int32_t y);
void W_Nvm_set(struct W_Nvm *w, uint32_t value);

#endif /* W_NVM_H_ */
