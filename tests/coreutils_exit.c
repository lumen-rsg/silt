#include <errno.h>
#include <setjmp.h>
#include <stdlib.h>
#include <unistd.h>

static jmp_buf finished;
static int result;
static int called;
static int failed;
static void capture_exit(int status) __attribute__((noreturn));
static void capture_exit(int status) { result = status; longjmp(finished, 1); }
#define atexit test_atexit
#define exit test_exit
#define _exit capture_exit
#include "../libc/exit.c"

static void late(void) { if (called != 32) failed = 1; called++; }
static void first(void) {
    if (called != 31) failed = 1;
    called++;
    if (atexit(late) != 0) failed = 1;
}
static void middle(void) {
    if (called < 1 || called > 30) failed = 1;
    called++;
}
static void last(void) { if (called != 0) failed = 1; called++; }
int main(void) {
    if (atexit(first)) return 1;
    for (int i = 0; i < 30; i++) if (atexit(middle)) return 1;
    if (atexit(last)) return 1;
    if (atexit(middle) != -1 || errno != ENOMEM) return 2;
    if (!setjmp(finished)) exit(17);
    if (failed || result != 17 || called != 33) return 3;
    return 0;
}
