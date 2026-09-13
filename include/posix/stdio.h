#pragma once

// Keep the toolchain FILE declaration compatible with wchar.h. Silt owns the
// three standard streams and their descriptor-backed implementation; newlib
// code and its reentrant stream globals are not linked.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include_next <stdio.h>
#pragma GCC diagnostic pop
#undef stdin
#undef stdout
#undef stderr
extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

// Disable newlib inline accessors: Silt keeps stream state separately.
#undef ferror
#undef clearerr
#undef fileno
#undef putchar
#undef fputc
#undef fputs
#undef fwrite
#undef fflush
#undef fclose
