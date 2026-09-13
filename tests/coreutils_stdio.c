// Deterministic standard-stream transport tests. The production implementation
// runs over injected short writes, EINTR, zero progress and terminal errors.
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char captured[4096];
static size_t captured_size;
static int calls;
static int mode;
static int allocation_fails;
static ssize_t transport_write(int fd, const void* data, size_t bytes) {
    calls++;
    if (fd < 0) { errno = EBADF; return -1; }
    if (mode == 1 && calls == 1) { errno = EINTR; return -1; }
    if (mode == 2 && calls > 1) { errno = ENOSPC; return -1; }
    if (mode == 3) return 0;
    if (bytes > 3) bytes = 3;
    if (captured_size + bytes > sizeof(captured)) abort();
    memcpy(captured + captured_size, data, bytes);
    captured_size += bytes;
    return (ssize_t)bytes;
}
static int transport_close(int fd) {
    if (fd < 0 || mode == 4) { errno = EBADF; return -1; }
    return 0;
}
static void* test_malloc(size_t bytes) { return allocation_fails ? NULL : malloc(bytes); }

#undef stdin
#undef stdout
#undef stderr
#define stdin test_stdin
#define stdout test_stdout
#define stderr test_stderr
#define fileno test_fileno
#define ferror test_ferror
#define clearerr test_clearerr
#define fflush test_fflush
#define fclose test_fclose
#define fwrite test_fwrite
#define fputc test_fputc
#define putchar test_putchar
#define fputs test_fputs
#define puts test_puts
#define vfprintf test_vfprintf
#define fprintf test_fprintf
#define printf test_printf
#define write transport_write
#define close transport_close
#define malloc test_malloc
#include "../libc/stdio.c"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
int main(void) {
    mode = 1;
    CHECK(fwrite("abcdefgh", 2, 4, stdout) == 4);
    CHECK(calls == 4 && captured_size == 8 && !memcmp(captured, "abcdefgh", 8));
    CHECK(!ferror(stdout));
    captured_size = 0; calls = 0; mode = 2;
    CHECK(fwrite("abcdefgh", 2, 4, stdout) == 1);
    CHECK(captured_size == 3 && ferror(stdout) && errno == ENOSPC);
    CHECK(fflush(stdout) == 0 && ferror(stdout));
    clearerr(stdout); calls = 0; mode = 3;
    CHECK(fwrite("a", 1, 1, stdout) == 0 && errno == EIO && ferror(stdout));
    clearerr(stdout); calls = 0; mode = 0;
    CHECK(fwrite("a", SIZE_MAX, 2, stdout) == 0 && errno == EOVERFLOW && calls == 0);
    CHECK(fwrite(NULL, 0, SIZE_MAX, stdout) == 0 && calls == 0);
    clearerr(stdout); captured_size = 0;
    CHECK(fputc(0, stdout) == 0 && captured_size == 1 && captured[0] == 0);
    CHECK(fputs("", stdout) >= 0);
    CHECK(fprintf(stdout, "%s:%d", "value", -7) == 8);
    CHECK(captured_size == 9 && !memcmp(captured + 1, "value:-7", 8));
    char large[600]; memset(large, 'x', sizeof(large) - 1); large[599] = 0;
    captured_size = 0;
    CHECK(fprintf(stdout, "%s", large) == 599 && captured_size == 599);
    allocation_fails = 1;
    CHECK(fprintf(stdout, "%s", large) == -1 && errno == ENOMEM && ferror(stdout));
    allocation_fails = 0; clearerr(stdout); mode = 4;
    CHECK(fclose(stdout) == EOF && ferror(stdout));
    CHECK(fileno(stdout) == -1 && errno == EBADF);
    CHECK(fputc('a', stdout) == EOF && errno == EBADF);
    CHECK(fflush(stdout) == EOF);
    return 0;
}
