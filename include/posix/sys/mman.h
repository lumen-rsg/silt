#pragma once

#include <stddef.h>

// dash includes this header for portability but does not use mappings in the
// current SMALL configuration. The declarations define Silt's future libc
// surface without claiming that the adapter is implemented yet.
void* mmap(void* address, size_t length, int protection, int flags,
           int descriptor, long offset);
int munmap(void* address, size_t length);
