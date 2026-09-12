#include "runtime.h"
#include <signal.h>
#include <sys/wait.h>

enum { CHILDREN = 6 };

// Neither the parent nor the runtime touches this RX page before release.
// Concurrent first fetches must coalesce or retry, never become SIGBUS.
__attribute__((noinline, noclone, used, aligned(4096)))
static unsigned cold_page(unsigned value) {
    __asm__ volatile("" : "+r"(value) : : "memory");
    return value ^ 0x55U;
}

__attribute__((aligned(4096)))
int main(void) {
    int64_t ready = sys_event_create(0, CHILDREN);
    int64_t release = sys_event_create(0, CHILDREN);
    if (ready <= 0 || release <= 0) return 10;

    pid_t children[CHILDREN];
    unsigned created = 0;
    int setup_error = 0;
    for (unsigned i = 0; i < CHILDREN; i++) {
        children[i] = fork();
        if (children[i] == 0) {
            if (sys_event_signal((uint32_t)ready, 1) != NEVA_STATUS_OK
                || sys_event_wait((uint32_t)release,
                       NEVA_DEADLINE_INFINITE, 0) != NEVA_STATUS_OK) {
                _exit(11);
            }
            _exit(cold_page(i) == (i ^ 0x55U) ? 0 : 12);
        }
        if (children[i] < 0) {
            setup_error = 13;
            goto abort_children;
        }
        created++;
    }

    for (unsigned i = 0; i < CHILDREN; i++) {
        if (sys_event_wait((uint32_t)ready,
                NEVA_DEADLINE_INFINITE, 0) != NEVA_STATUS_OK) {
            setup_error = 14;
            goto abort_children;
        }
    }
    if (sys_event_signal((uint32_t)release, CHILDREN) != NEVA_STATUS_OK) {
        setup_error = 15;
        goto abort_children;
    }

    int failed = 0;
    for (unsigned i = 0; i < CHILDREN; i++) {
        int status = -1;
        if (waitpid(children[i], &status, 0) != children[i]
            || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            failed = 1;
        }
    }
    sys_handle_close((uint32_t)ready);
    sys_handle_close((uint32_t)release);
    if (failed) return 16;
    neva_println("D4_PAGER_SMP: concurrent cold RX faults PASS");
    return 0;

abort_children:
    for (unsigned i = 0; i < created; i++) {
        (void)kill(children[i], SIGKILL);
    }
    for (unsigned i = 0; i < created; i++) {
        (void)waitpid(children[i], NULL, 0);
    }
    sys_handle_close((uint32_t)ready);
    sys_handle_close((uint32_t)release);
    return setup_error;
}
