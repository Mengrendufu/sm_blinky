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
// text. The container is a 236x20 flex row with 2 px padding on all
// four sides; groups keep the LED+count left and FPS+CPU right.
// A parent layout or caller MAY position the object, but that is not
// the 2 px edge-spacing contract -- the padding lives inside Flex.
//
// Figma used Inter for some labels; this component draws text with
// the compiled-in Montserrat 14, so internal widths are adapted to keep
// the maximum values ("4294967295", "999FPS", "100%") unclipped.
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

void UiTopbar_create(UiTopbar *me, lv_obj_t *parent);
void UiTopbar_destroy(UiTopbar *me);
void UiTopbar_setLed(UiTopbar *me, bool on);
void UiTopbar_setCount(UiTopbar *me, uint32_t count);
void UiTopbar_setFps(UiTopbar *me, uint16_t fps);
void UiTopbar_setCpu(UiTopbar *me, uint8_t percent);

#endif // TOPBAR_H_
