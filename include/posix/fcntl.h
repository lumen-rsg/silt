#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include_next <fcntl.h>
#pragma GCC diagnostic pop

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif

#ifndef O_DIRECTORY
#define O_DIRECTORY 0x100000
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0x400000
#endif

#ifndef F_DUPFD_CLOEXEC
#define F_DUPFD_CLOEXEC 14
#endif

int openat(int directory, const char* path, int flags, ...);
