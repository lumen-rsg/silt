#pragma once

// dash build configuration for the first Silt bring-up stage. This describes
// implemented link-time facilities, not merely declarations available in the
// cross toolchain's headers.

#define PACKAGE "dash"
#define PACKAGE_NAME "dash"
#define PACKAGE_STRING "dash 0.5.13.5 for Silt"
#define PACKAGE_TARNAME "dash"
#define PACKAGE_VERSION "0.5.13.5"

#define _GNU_SOURCE 1
#define HAVE_ALIAS_ATTRIBUTE 1
#define HAVE_ALLOCA_H 1
#define HAVE_DECL_ISBLANK 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_UNISTD_H 1
#define HAVE_WCHAR_H 1

// libsilt already supplies these calls, although their full POSIX semantics
// remain part of the dash port acceptance matrix.
#define HAVE_ISALPHA 1
#define HAVE_KILLPG 1
#define HAVE_MEMRCHR 1
#define HAVE_STRTOD 1
#define HAVE_STPCPY 1
#define HAVE_STRSIGNAL 1
#define HAVE_WAIT3 1

#define HAVE_F_DUPFD_CLOEXEC 0
#define SIZEOF_INTMAX_T 8
#define SIZEOF_LONG_LONG_INT 8
#define USE_MEMFD_CREATE 0
#define USE_TEE 0

// Process groups and terminal foreground changes use capability-safe adapters.
#define JOBS 1
#define SMALL 1

#define _PATH_BSHELL "/bin/sh"
#define _PATH_DEVNULL "/dev/null"
#define _PATH_TTY "/dev/tty"

// Silt uses one 64-bit file ABI. dash retains these compatibility names for
// libc variants that expose separate large-file entry points.
#define dirent64 dirent
#define fstat64 fstat
#define lstat64 lstat
#define open64 open
#define readdir64 readdir
#define stat64 stat
