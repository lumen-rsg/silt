#include "runtime.h"
#include "session_control.h"
#include <errno.h>
#include <signal.h>
#include <silt_pipeline.h>
#include <sys/wait.h>
#include <termios.h>

enum { ATTR, WRITE, FOREGROUND, AUTHORITY };
enum { DEFAULT, IGNORED, BLOCKED, CAUGHT };
static volatile sig_atomic_t g_caught;

static void caught(int sig) { (void)sig; g_caught++; }

static int check_event_observer(void) {
    int64_t created = sys_event_create(1, 1);
    if (created <= 0) return 30;
    uint32_t event = (uint32_t)created;
    uint32_t observer = sys_handle_dup(event, HANDLE_RIGHT_READ);
    uint32_t writer = sys_handle_dup(event, HANDLE_RIGHT_WRITE);
    if (!observer || !writer) return 31;
    for (int i = 0; i < 160; i++) {
        if (sys_event_wait(observer, 0, EVENT_WAIT_NONBLOCK | EVENT_WAIT_PEEK) != NEVA_STATUS_OK) return 32;
    }
    if (sys_event_wait(event, 0, EVENT_WAIT_NONBLOCK) != NEVA_STATUS_OK
        || sys_event_wait(observer, 0, EVENT_WAIT_NONBLOCK | EVENT_WAIT_PEEK) != NEVA_STATUS_WOULD_BLOCK
        || sys_event_wait(observer, NEVA_DEADLINE_INFINITE, EVENT_WAIT_PEEK) != NEVA_STATUS_INVALID_ARGUMENT
        || sys_event_wait(observer, 0, 8U) != NEVA_STATUS_INVALID_ARGUMENT
        || sys_event_wait(writer, 0, EVENT_WAIT_NONBLOCK | EVENT_WAIT_PEEK) != NEVA_STATUS_BAD_HANDLE) return 33;
    sys_handle_close(observer);
    if (sys_event_wait(observer, 0, EVENT_WAIT_NONBLOCK | EVENT_WAIT_PEEK) != NEVA_STATUS_BAD_HANDLE) return 34;
    sys_handle_close(writer);
    sys_handle_close(event);
    neva_println("D4_EVENT_PEEK: observer/consumer/empty/flags/rights/stale PASS");
    return 0;
}

static int configure(int disposition) {
    struct sigaction action = { .sa_handler = disposition == DEFAULT ? SIG_DFL
        : disposition == IGNORED ? SIG_IGN : caught };
    sigset_t mask;
    sigemptyset(&mask);
    if (disposition == BLOCKED) sigaddset(&mask, SIGTTOU);
    return sigaction(SIGTTOU, &action, NULL) < 0
        || sigprocmask(SIG_SETMASK, &mask, NULL) < 0 ? -1 : 0;
}

static int child_check(int operation, int disposition, uint32_t parent_group) {
    if (configure(disposition) < 0) return 10;
    g_caught = 0;
    struct termios attributes;
    if (tcgetattr(0, &attributes) < 0) return 11;
    attributes.c_cc[VERASE] = 8;
    pid_t peer = -1;
    uint32_t ready = 0, release = 0;
    if (disposition == IGNORED || disposition == BLOCKED) {
        int64_t first = sys_event_create(0, 1);
        int64_t second = sys_event_create(0, 1);
        if (first <= 0 || second <= 0) return 19;
        ready = (uint32_t)first;
        release = (uint32_t)second;
        peer = fork();
        if (peer == 0) {
            if (configure(DEFAULT) < 0) _exit(20);
            sys_event_signal(ready, 1);
            if (sys_event_wait(release, NEVA_DEADLINE_INFINITE, 0) != NEVA_STATUS_OK) _exit(21);
            _exit(0);
        }
        if (peer < 0) return 12;
        if (sys_event_wait(ready, NEVA_DEADLINE_INFINITE, 0) != NEVA_STATUS_OK) return 22;
    }
    int result;
    if (operation == ATTR) result = tcsetattr(0, TCSANOW, &attributes);
    else if (operation == WRITE) result = write(1, ".", 1);
    else if (operation == FOREGROUND) result = tcsetpgrp(0, getpgrp());
    else {
        NevaTtyAttributesV1 request = {
            .magic = NEVA_TTY_ATTRIBUTES_MAGIC, .version = NEVA_TTY_ABI_VERSION,
            .size = sizeof(request), .flags = NEVA_TTY_FLAG_CANONICAL | NEVA_TTY_FLAG_ECHO
                | NEVA_TTY_FLAG_SIGNALS, .erase = 8, .kill = 21, .eof = 4, .intr = 3, .susp = 26,
        };
        uint32_t tty = neva_tty_handle();
        NevaServiceResult missing = sys_service_call(tty, PTY_SLAVE_RPC_SET_ATTRIBUTES,
            (uintptr_t)&request, 0, 0, sizeof(request), NEVA_DEADLINE_INFINITE);
        NevaServiceResult wrong = sys_service_call(tty, PTY_SLAVE_RPC_SET_ATTRIBUTES,
            (uintptr_t)&request, 0, parent_group, sizeof(request), NEVA_DEADLINE_INFINITE);
        result = missing.status == NEVA_STATUS_ACCESS_DENIED
            && wrong.status == NEVA_STATUS_ACCESS_DENIED ? 0 : -1;
    }
    int operation_errno = errno;
    if (disposition == CAUGHT) {
        if (result != -1 || operation_errno != EINTR || g_caught != 1) return 13;
    } else if (result != (operation == WRITE ? 1 : 0)) {
        configure(IGNORED);
        neva_print("D4_TERMINAL unexpected result="); neva_print_int((uint64_t)(int64_t)result);
        neva_print(" errno="); neva_print_int((uint64_t)operation_errno); neva_putc('\n');
        return 14;
    }
    if (operation == FOREGROUND && result == 0
        && neva_tty_set_foreground(neva_tty_handle(), parent_group) != NEVA_STATUS_OK) return 15;
    if (disposition == BLOCKED) {
        sigset_t empty;
        sigemptyset(&empty);
        if (sigprocmask(SIG_SETMASK, &empty, NULL) < 0 || g_caught != 0) return 16;
    }
    if (peer > 0) {
        sys_event_signal(release, 1);
        int status;
        if (waitpid(peer, &status, WUNTRACED) != peer) return 17;
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            kill(peer, SIGKILL);
            waitpid(peer, &status, 0);
            return 18;
        }
        sys_handle_close(ready);
        sys_handle_close(release);
    }
    return 0;
}

static int run_case(int operation, int disposition, int tostop,
                     uint32_t parent_group, const struct termios* baseline) {
    struct termios attributes = *baseline;
    attributes.c_lflag = ICANON | ECHO | ISIG | (tostop ? TOSTOP : 0);
    attributes.c_cc[VERASE] = 127;
    if (tcsetattr(0, TCSANOW, &attributes) < 0) return 1;
    silt_job_prepare(0, 0);
    pid_t child = fork();
    if (child == 0) _exit(child_check(operation, disposition, parent_group));
    if (child < 0) return 2;
    silt_job_finish(child);
    int status;
    if (waitpid(child, &status, WUNTRACED) != child) return 3;
    int expect_stop = disposition == DEFAULT && (operation != WRITE || tostop);
    if (expect_stop) {
        if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGTTOU) return 4;
        if (tcgetattr(0, &attributes) < 0 || attributes.c_cc[VERASE] != 127
            || tcgetpgrp(0) != getpgrp()) return 5;
        if (tcsetpgrp(0, child) < 0 || kill(child, SIGCONT) < 0
            || waitpid(child, &status, 0) != child) return 6;
    }
    if (tcsetpgrp(0, getpgrp()) < 0) return 7;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        neva_print("D4_TERMINAL child status=");
        neva_print_int((uint64_t)status);
        neva_putc('\n');
        return 8;
    }
    if (tcgetattr(0, &attributes) < 0
        || attributes.c_cc[VERASE] != (operation == ATTR && disposition != CAUGHT ? 8 : 127)) return 9;
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc == 2 && strcmp(argv[1], "peek") == 0) return check_event_observer();
    if (argc == 2) {
        struct termios attributes;
        if (tcgetattr(0, &attributes) < 0) return 1;
        if (strcmp(argv[1], "show-erase") == 0) {
            neva_print("D4_ERASE="); neva_print_int(attributes.c_cc[VERASE]); neva_putc('\n');
            return 0;
        }
        if (strcmp(argv[1], "set-erase") != 0 && strcmp(argv[1], "reset-erase") != 0) return 64;
        attributes.c_cc[VERASE] = strcmp(argv[1], "set-erase") == 0 ? 8 : 127;
        if (configure(DEFAULT) < 0 || tcsetattr(0, TCSANOW, &attributes) < 0) return 2;
        neva_println("D4_ERASE_SET");
        return 0;
    }
    struct termios baseline;
    uint32_t catalog = silt_startup_handle("catalog", NEVA_OBJECT_TYPE_REMOTE_OBJECT);
    NevaServiceResult own = sys_service_call(catalog, SESSION_CONTROL_RPC_DUP_OWN_GROUP,
        0, 0, 0, 0, NEVA_DEADLINE_INFINITE);
    if (own.status != NEVA_STATUS_OK || !own.handle || tcgetattr(0, &baseline) < 0) return 1;
    // The test parent acts as a miniature job-control shell.
    if (configure(IGNORED) < 0) return 2;
    for (int operation = ATTR; operation <= FOREGROUND; operation++) {
        for (int disposition = DEFAULT; disposition <= CAUGHT; disposition++) {
            for (int tostop = 0; tostop < 2; tostop++) {
                // Writes without TOSTOP do not generate a caught signal.
                if (operation == WRITE && disposition == CAUGHT && !tostop) continue;
                int result = run_case(operation, disposition, tostop, own.handle, &baseline);
                if (result) {
                    neva_print("D4_TERMINAL: FAIL case=");
                    neva_print_int((uint64_t)(operation * 100 + disposition * 10 + tostop));
                    neva_print(" code="); neva_print_int((uint64_t)result); neva_putc('\n');
                    return result;
                }
            }
        }
    }
    if (run_case(AUTHORITY, IGNORED, 0, own.handle, &baseline)) return 3;
    if (tcsetattr(0, TCSANOW, &baseline) < 0) return 4;
    sys_handle_close(own.handle);
    neva_println("D4_TERMINAL: stop/resume/ignore/block/catch/authority PASS");
    return 0;
}
