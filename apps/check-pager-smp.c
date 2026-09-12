#include "runtime.h"
#include <signal.h>
#include <sys/wait.h>

// The session budget is eight: nsh, this fixture, five watchers and one churn child.
enum { WATCHERS = 5, LIFETIMES = 272 };
static volatile uint64_t g_pattern __attribute__((aligned(4096)));

static int wait_success(pid_t child) {
    int status = -1;
    return waitpid(child, &status, 0) == child
        && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int check_asid(void) {
    int64_t ready = sys_event_create(0, WATCHERS);
    int64_t stop = sys_event_create(0, WATCHERS);
    if (ready <= 0 || stop <= 0) {
        if (ready > 0) sys_handle_close((uint32_t)ready);
        if (stop > 0) sys_handle_close((uint32_t)stop);
        return 10;
    }
    pid_t watchers[WATCHERS];
    unsigned created = 0;
    int error = 0;
    g_pattern = 0x13579bdf2468ace0ULL;
    for (unsigned i = 0; i < WATCHERS; i++) {
        pid_t child = fork();
        if (child == 0) {
            uint64_t expected = 0xc001000000000000ULL + i;
            g_pattern = expected;
            if (sys_event_signal((uint32_t)ready, 1) != NEVA_STATUS_OK) _exit(11);
            for (;;) {
                if (g_pattern != expected) _exit(12);
                NevaStatus status = sys_event_wait((uint32_t)stop,
                    NEVA_DEADLINE_INFINITE, EVENT_WAIT_NONBLOCK);
                if (status == NEVA_STATUS_OK) break;
                if (status != NEVA_STATUS_WOULD_BLOCK) _exit(13);
                sys_yield();
            }
            _exit(g_pattern == expected ? 0 : 14);
        }
        if (child < 0) { error = 15; goto cleanup; }
        watchers[created++] = child;
    }
    for (unsigned i = 0; i < WATCHERS; i++) {
        if (sys_event_wait((uint32_t)ready,
                NEVA_DEADLINE_INFINITE, 0) != NEVA_STATUS_OK) {
            error = 16;
            goto cleanup;
        }
    }
    // Keep the parent and five private same-VA owners alive across more than
    // one complete 8-bit tag namespace, while transient children churn tags.
    for (unsigned i = 0; i < LIFETIMES; i++) {
        uint64_t expected = 0x13579bdf2468ace0ULL ^ i;
        g_pattern = expected;
        pid_t child = fork();
        if (child == 0) {
            g_pattern = ~expected;
            for (unsigned round = 0; round < 8; round++) {
                sys_yield();
                if (g_pattern != ~expected) _exit(17);
            }
            _exit(0);
        }
        if (child < 0) { error = 18; goto cleanup; }
        for (unsigned round = 0; round < 8; round++) {
            sys_yield();
            if (g_pattern != expected) error = 19;
        }
        if (!wait_success(child)) error = 20;
        if (error) goto cleanup;
    }
    if (sys_event_signal((uint32_t)stop, WATCHERS) != NEVA_STATUS_OK) {
        error = 21;
        goto cleanup;
    }

cleanup:
    if (error) {
        for (unsigned i = 0; i < created; i++) (void)kill(watchers[i], SIGKILL);
    }
    for (unsigned i = 0; i < created; i++) {
        if (!wait_success(watchers[i]) && !error) error = 22;
    }
    sys_handle_close((uint32_t)ready);
    sys_handle_close((uint32_t)stop);
    if (error) {
        neva_print("D4_ASID: FAIL code=");
        neva_print_int((uint64_t)error);
        neva_println("");
        return error;
    }
    neva_println("D4_ASID: long-lived owners across 272 fork lifetimes PASS");
    return 0;
}

enum { CHILDREN = 6 };

// Neither the parent nor the runtime touches this RX page before release.
// Concurrent first fetches must coalesce or retry, never become SIGBUS.
__attribute__((noinline, noclone, used, aligned(4096)))
static unsigned cold_page(unsigned value) {
    __asm__ volatile("" : "+r"(value) : : "memory");
    return value ^ 0x55U;
}

__attribute__((aligned(4096)))
int main(int argc, char* argv[]) {
    if (argc > 1) return argc == 2 && strcmp(argv[1], "asid") == 0 ? check_asid() : 2;
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
