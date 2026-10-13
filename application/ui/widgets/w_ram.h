//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software; it comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
//============================================================================
#ifndef W_RAM_H_
#define W_RAM_H_

#include <stdint.h>

struct _lv_obj_t;

// The on-chip RAM, as the linker laid it out: how much of the static image
// uses it, against the top of the stack. Both lines are set once - nothing
// here moves at run time.
struct W_Ram {
    struct _lv_obj_t *lineA;   // "RAM 57.9 / 64.0 KB"
    struct _lv_obj_t *lineB;   // "free 6.1 KB"
    struct _lv_obj_t *lineC;   // "msp 412 / 1024 B"
    // What lineC showed last. It lives here, not in a file static: the labels
    // are made fresh on every page entry, so the memory of "what is already
    // written" has to be dropped with them.
    uint32_t shownMsp;
};

void W_Ram_init(struct W_Ram *r);
void W_Ram_create(struct W_Ram *r, struct _lv_obj_t *parent);
void W_Ram_set(struct W_Ram *r, uint32_t usedBytes, uint32_t totalBytes);
// The main stack's peak, in bytes, against what the linker reserves for it.
// It only ever grows, and the label is only rewritten when it does.
void W_Ram_setMsp(struct W_Ram *r, uint32_t peakBytes,
                   uint32_t reservedBytes);

#endif // W_RAM_H_
