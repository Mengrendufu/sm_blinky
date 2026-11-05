//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef TOPBAR_H_
#define TOPBAR_H_

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

// Passive status strip: LED indicator, LED edge counter, FPS and CPU
// text. The container is a 236x17 flex row without padding.
// The parent owns page-edge spacing (UiRoot_init sets padding to 2).
// Groups keep LED+count left and FPS+CPU right; labels align right.
//
// Figma and the compiled font are Montserrat 14, with slightly different
// metrics. Count/FPS boxes extend 1/3 px left of Figma's bounds to fit
// maximum values while retaining its text right edges.
// Domains: fps 0..999, cpu 0..100, count full uint32.
//
// Contracts: zero-initialize the instance before create; never copy
// or move a live instance; UI thread only. The parent must outlive
// the component; destroy it before deleting the parent. The text
// buffers inside the instance are lent to LVGL via static labels,
// so they must stay alive while shown.
typedef struct {
    lv_obj_t *container;
    lv_obj_t *led;
    lv_obj_t *blinkyCount;
    lv_obj_t *fps;
    lv_obj_t *cpu;
    char blinkyCountText[11]; // "4294967295"
    char fpsText[8];    // "999FPS"
    char cpuText[5];    // "100%"
} UiTopbar;

// Outer dimensions accept native LVGL pixels or lv_pct().
void UiTopbar_create(UiTopbar *me, lv_obj_t *parent,
                      int32_t width, int32_t height);
void UiTopbar_destroy(UiTopbar *me);
void UiTopbar_setLed(UiTopbar *me, bool on);
void UiTopbar_setCount(UiTopbar *me, uint32_t count);
void UiTopbar_setFps(UiTopbar *me, uint16_t fps);
void UiTopbar_setCpu(UiTopbar *me, uint8_t percent);

#endif // TOPBAR_H_
