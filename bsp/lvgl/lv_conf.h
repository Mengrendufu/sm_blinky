//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef LV_CONF_H
#define LV_CONF_H

//============================================================================
// LVGL v9 configuration for this board. Only what differs from LVGL's own
// defaults (src/lv_conf_internal.h) is listed here, so upgrading LVGL does
// not mean diffing a thousand line template. Widgets, drawing and fonts
// stay at their defaults for now; trim them once the UI is known.

// Display: ST7789 over the 8080 16-bit interface, one RGB565 word per pixel.
#define LV_COLOR_DEPTH 16

// The draw buffer and LVGL's own pool are plain arrays in .bss; give them the
// alignment the renderer expects (LV_DRAW_BUF_ALIGN is 4 on this build).
#define LV_ATTRIBUTE_MEM_ALIGN __attribute__((aligned(4)))

// Memory: LVGL keeps its own TLSF pool in a static array (work_mem_int in the
// link map), same "everything static" rule the rest of the firmware follows.
// 24 KB: the 16 KB first guess is not far off what the trimmed UI needs, but
// LVGL's own requirements page asks for > 48 KB with many widgets and the
// failure mode of a full pool is a hard to read assert, so the pool grows
// while the RAM is there. Every KB here is a KB of .bss.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE          (24U * 1024U)
#define LV_MEM_ADR           0

// Nothing behind us but newlib-without-a-heap: use LVGL's own string and
// format code so the firmware stays free of libc malloc/printf.
#define LV_USE_STDLIB_STRING  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN

// LVGL is driven from an AO (lv_timer_handler on a FreeRTOS tick). Its own
// FreeRTOS layer would want dynamic tasks, which this firmware does not have:
// configSUPPORT_DYNAMIC_ALLOCATION is 0.
#define LV_USE_OS LV_OS_NONE

// Tracing goes through sm_tracer; LVGL's own log stays off.
#define LV_USE_LOG 0

// Assertions follow the project rule (see AGENTS.md): losing the log is fine,
// hanging is not. Same handler as configASSERT.
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_MEM_INTEGRITY 0
// LVGL expands LV_ASSERT_HANDLER as a complete statement, so the semicolon is
// part of the macro (its own default is "while(1);").
#define LV_ASSERT_HANDLER_INCLUDE   "sm_assert.h"
#define LV_ASSERT_HANDLER           SM_onAssert(__FILE__, __LINE__);

//============================================================================
// Trim. LVGL v9 switches every widget and every draw format on by default,
// and the default theme names every widget class, so an unused chart still
// lands in the image: this build was 475 KB (91% of the 512 KB part) before
// the list below, 404 KB of it LVGL. What is kept is what the UI draws with -
// obj, label (and its span helper), bar, led, line, the default theme and
// Montserrat 14. Each line is one feature back; add it when a screen
// needs it.

// Widgets nobody draws yet.
#define LV_USE_ARC        1
#define LV_USE_ARCLABEL   0
#define LV_USE_BUTTON     0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR   0
#define LV_USE_CANVAS     0
#define LV_USE_CHART      0
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   0
#define LV_USE_IMAGE      1
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD   0
#define LV_USE_LIST       0
#define LV_USE_MENU       0
#define LV_USE_MSGBOX     0
#define LV_USE_ROLLER     0
#define LV_USE_SCALE      0
#define LV_USE_SLIDER     0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    1
#define LV_USE_SWITCH     1
#define LV_USE_TABLE      0
#define LV_USE_TABVIEW    0
#define LV_USE_TEXTAREA   0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0
#define LV_USE_ANIMIMG    0

// Layout engines: this UI positions everything explicitly.
#define LV_USE_FLEX       0
#define LV_USE_GRID       0

// Machinery for applications bigger than this one.
#define LV_USE_OBSERVER         0
#define LV_USE_OBJ_ID           0
#define LV_USE_OBJ_ID_BUILTIN   0
#define LV_USE_OBJ_PROPERTY     0
#define LV_USE_SNAPSHOT         0
#define LV_USE_MONKEY           0
#define LV_USE_GRIDNAV          0
#define LV_USE_GESTURE_RECOGNITION 0
#define LV_USE_FONT_MANAGER     0
#define LV_USE_TRANSLATION      0
#define LV_USE_FRAGMENT         0
#define LV_USE_EXT_DATA         0
#define LV_USE_FILE_EXPLORER    0
#define LV_USE_IMGFONT          0
#define LV_USE_FONT_COMPRESSED  0
#define LV_USE_BIDI             0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

// Draw formats. RGB565 is the panel, A8 is what font glyphs and masks blend
// through; everything else only exists for files, images or other panels.
// If LVGL ever asserts about an unsupported format, re-enable the one it
// names - ARGB8888 first, it is the layer format.
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED      0
#define LV_DRAW_SW_SUPPORT_RGB565A8            0
#define LV_DRAW_SW_SUPPORT_RGB888              0
#define LV_DRAW_SW_SUPPORT_XRGB8888            0
#define LV_DRAW_SW_SUPPORT_ARGB8888            1
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_I1                  0
#define LV_DRAW_SW_SUPPORT_L8                  0
#define LV_DRAW_SW_SUPPORT_AL88                0

#endif // LV_CONF_H
