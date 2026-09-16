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

// Memory: LVGL keeps its own TLSF pool in a static array (work_mem_int in the
// link map), same "everything static" rule the rest of the firmware follows.
// 16 KB is sized from the measured structure sizes; LVGL's 64 KB default is
// far too much for this part. Raising it only means a larger .bss.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE          (16U * 1024U)
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

#endif // LV_CONF_H
