//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef UI_LIVE_PAGE_H_
#define UI_LIVE_PAGE_H_

#include "widgets/w_dial.h"
#include "widgets/w_switch.h"
#include "widgets/w_nvm.h"
#include "widgets/w_ram.h"
#include "widgets/w_dots.h"

// Live presentation subsystem. Borrows the exclusive content root
// and owns the widgets created below it.
typedef struct {
    struct _lv_obj_t *root;
    struct W_Dial dial;
    struct W_Switch sw;
    struct W_Nvm nvm;
    struct W_Ram ram;
    struct W_Dots dots;
} UiLivePage;

void UiLivePage_init(UiLivePage *me);
void UiLivePage_create(UiLivePage *me, struct _lv_obj_t *parent,
                       uint32_t ramUsed, uint32_t ramTotal);
void UiLivePage_setLed(UiLivePage *me, bool on);
void UiLivePage_setCounter(UiLivePage *me, uint32_t value);
void UiLivePage_stepArc(UiLivePage *me);
void UiLivePage_destroy(UiLivePage *me);

#endif // UI_LIVE_PAGE_H_
