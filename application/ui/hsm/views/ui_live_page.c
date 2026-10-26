//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "ui_live_page.h"
#include "lvgl.h"

void UiLivePage_init(UiLivePage *me) {
    *me = (UiLivePage){0};
    W_Dial_init(&me->dial);
    W_Switch_init(&me->sw);
    W_Nvm_init(&me->nvm);
    W_Ram_init(&me->ram);
}

void UiLivePage_create(UiLivePage *me, struct _lv_obj_t *parent,
                       uint32_t ramUsed, uint32_t ramTotal) {
    me->root = parent;
    W_Dial_create(&me->dial, me->root, 92, 40, 96);
    W_Switch_create(&me->sw, me->root, 12, 74);
    W_Nvm_create(&me->nvm, me->root, 12, 181);
    W_Ram_create(&me->ram, me->root);
    W_Ram_set(&me->ram, ramUsed, ramTotal);
    W_Dots_create(&me->dots, me->root, 1U);
}

void UiLivePage_setLed(UiLivePage *me, bool on) {
    W_Switch_set(&me->sw, on);
}

void UiLivePage_setCounter(UiLivePage *me, uint32_t value) {
    W_Nvm_set(&me->nvm, value);
}

void UiLivePage_stepArc(UiLivePage *me) {
    W_Dial_step(&me->dial);
}

void UiLivePage_destroy(UiLivePage *me) {
    lv_obj_clean((lv_obj_t *)me->root);
    UiLivePage_init(me);
}
