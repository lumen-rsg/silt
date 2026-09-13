#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include_next <inttypes.h>
#pragma GCC diagnostic pop

// The bare-metal newlib configuration does not identify AArch64 long as the
// 64-bit intmax_t carrier, although the compiler ABI does. Keep dash's format
// checking and runtime formatting aligned with the actual target type.
#undef PRIdMAX
#undef PRIiMAX
#define PRIdMAX "ld"
#define PRIiMAX "li"
