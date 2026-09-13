#pragma once
#define _GNU_SOURCE 1

// Audited C1 configuration, not host configure output. Only the selected
// command and support sources consume this file. Silt has C-locale paths.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define _GL_CONFIG_H_INCLUDED 1
#define GNULIB_DIRNAME 1
#define GNULIB_XALLOC 1
#define GNULIB_XALLOC_DIE 1
#define DOUBLE_SLASH_IS_DISTINCT_ROOT 0
#define _GL_ATTRIBUTE_MALLOC __attribute__((malloc))
#define _GL_ATTRIBUTE_DEALLOC_FREE
#define _GL_ATTRIBUTE_PURE __attribute__((pure))
#define _GL_ATTRIBUTE_RETURNS_NONNULL __attribute__((returns_nonnull))
#define _GL_ATTRIBUTE_ALLOC_SIZE(args) __attribute__((alloc_size args))
#define _GL_INLINE_HEADER_BEGIN
#define _GL_INLINE_HEADER_END
#define _GL_INLINE static inline
#define __getopt_argv_const const
#define GNULIB_TEXT_DOMAIN "coreutils"
#define PACKAGE "coreutils"
#define PACKAGE_NAME "GNU coreutils"
#define LOCALEDIR "/usr/share/locale"
#define _GL_UNUSED __attribute__((unused))

#define HAVE_COPY_FILE_RANGE 0
#define HAVE_SPLICE 0
#define HAVE_STROPTS_H 0
#define MAYBE_UNUSED __attribute__((unused))

#define _GL_ATTRIBUTE_NONNULL(args) __attribute__((nonnull args))

#define SILT_FINITE_TAIL 1
#define SILT_C_LOCALE 1
#define HAVE_INOTIFY 0
#define HAVE_FIFO_PIPES 1

#ifndef S_TYPEISSHM
#define S_TYPEISSHM(s) 0
#endif
#ifndef S_TYPEISTMO
#define S_TYPEISTMO(s) 0
#endif

#define _GL_ATTRIBUTE_SENTINEL(args) __attribute__((sentinel args))
