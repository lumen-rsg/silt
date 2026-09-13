#pragma once
#include "silt-system.h"
#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdckdint.h>
#include <sys/stat.h>
#include "xalloc.h"
#include "quote.h"

#define proper_name_lite(ascii, utf8) (utf8)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define SAME_INODE(a, b) ((a).st_ino == (b).st_ino && (a).st_dev == (b).st_dev)
#define S_TYPEISSHM(s) 0
#define S_TYPEISTMO(s) 0
#undef O_BINARY
#define O_BINARY 0
#define FADVISE_SEQUENTIAL 0
#define STP_BLKSIZE(s) 4096
#define oprintf printf
#define main_exit exit
#define quotef quote
#define quoteaf quote
#define quoteaf_n(n, s) quote(s)
#define c_isdigit(c) ((unsigned)(c) - '0' < 10)
#define to_uchar(c) ((unsigned char)(c))
#ifndef unreachable
#define unreachable() __builtin_unreachable()
#endif
#define IF_LINT(code)
#include "xdectoint.h"

static inline idx_t io_blksize(const struct stat* st) { (void)st; return 4096; }
static inline bool usable_st_size(const struct stat* st) { return S_ISREG(st->st_mode); }
// Advisory access hints and binary/text translation do not alter Silt I/O.
static inline void fdadvise(int fd, off_t offset, off_t size, int advice) {
    (void)fd; (void)offset; (void)size; (void)advice;
}
static inline void xset_binary_mode(int fd, int mode) { (void)fd; (void)mode; }
int getpagesize(void);
void* rawmemchr(const void* buffer, int byte);
void emit_stdin_note(void);
void emit_size_note(void);
void write_error(void) __attribute__((noreturn));
void* xalignalloc(idx_t alignment, idx_t size);
void alignfree(void* pointer);
size_t full_write(int fd, const void* buffer, size_t count);
size_t full_read(int fd, void* buffer, size_t count);
ssize_t silt_safe_read(int fd, void* buffer, size_t count);
#define read silt_safe_read
uintmax_t xnumtoumax(const char* text, int base, uintmax_t min, uintmax_t max,
                    const char* suffixes, const char* message, int status, int flags);
intmax_t xnumtoimax(const char* text, int base, intmax_t min, intmax_t max,
                  const char* suffixes, const char* message, int status, int flags);

#ifndef SSIZE_MAX
#define SSIZE_MAX PTRDIFF_MAX
#endif
static inline bool is_ENOTSUP(int number) { return number == ENOTSUP || number == EOPNOTSUPP; }
static inline ssize_t copy_file_range(int in, off_t* ip, int out, off_t* op, size_t size, unsigned flags) {
    (void)in; (void)ip; (void)out; (void)op; (void)size; (void)flags;
    errno = ENOSYS;
    return -1;
}

#include <time.h>
#define gettext(text) (text)
#define ARGMATCH_VERIFY(strings, values) _Static_assert(sizeof(strings) / sizeof(*(strings)) == 1 + sizeof(values) / sizeof(*(values)), "argument map")
#define XARGMATCH(option, arg, strings, values) ((values)[silt_argmatch(option, arg, strings)])
#define OFF_T_MAX INT64_MAX
#define ATTRIBUTE_PURE __attribute__((pure))
int silt_argmatch(const char* option, const char* argument, const char* const* values);
void silt_unsupported(const char* feature) __attribute__((noreturn));
int posix2_version(void);
char* umaxtostr(uintmax_t value, char* buffer);
char* imaxtostr(intmax_t value, char* buffer);

#include "intprops.h"
