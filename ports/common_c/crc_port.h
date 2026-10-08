//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef CRC_PORT_H_
#define CRC_PORT_H_

// The port header common_c's crc32.c expects and the vendored copy does not
// ship. It only needs the fixed-width types: the implementation is plain C
// a const table, and there is nothing platform specific left to bind.
#include <stdint.h>

// crc32.c puts its 1 KB lookup table in this: "read only memory", i.e. plain
// const, which the linker keeps in .rodata (flash) rather than in RAM.
#define SNMTT_ROM const

#endif // CRC_PORT_H_
