#include "runtime.h"
#include "session_control.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <termios.h>

int silt_check_sessions(int argc, char** argv);
int silt_check_exec(int argc, char** argv);

static volatile sig_atomic_t g_received;
static volatile sig_atomic_t g_mask_valid;

static void receive(int sig) {
    sigset_t mask;
    (void)sigprocmask(SIG_SETMASK, NULL, &mask);
    g_mask_valid = sigismember(&mask, sig) && sigismember(&mask, SIGUSR2);
    g_received++;
}

static int run_checks(int argc, char* argv[]) {
    if (argc == 2) {
        struct termios mode;
        if (tcgetattr(0, &mode) < 0) return 20;
        if (strcmp(argv[1], "tostop") == 0) mode.c_lflag |= TOSTOP;
        else mode.c_lflag &= ~TOSTOP;
        return tcsetattr(0, TCSANOW, &mode) < 0 ? 21 : 0;
    }
    struct sigaction action = { .sa_handler = receive };
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGUSR2);
    if (sigaction(SIGUSR1, &action, NULL) < 0) return 1;
    struct sigaction query;
    if (sigaction(SIGUSR1, NULL, &query) < 0 || query.sa_handler != receive
        || !sigismember(&query.sa_mask, SIGUSR2)) return 2;
    sigset_t blocked, empty, old;
    sigemptyset(&empty);
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGUSR1);
    sigaddset(&blocked, SIGKILL);
    sigaddset(&blocked, SIGSTOP);
    if (sigprocmask(SIG_BLOCK, &blocked, NULL) < 0 || raise(SIGUSR1) < 0) return 3;
    if (g_received || sigprocmask(SIG_SETMASK, NULL, &old) < 0
        || sigismember(&old, SIGKILL) || sigismember(&old, SIGSTOP)) return 4;
    if (sigsuspend(&empty) != -1 || errno != EINTR || g_received != 1 || !g_mask_valid) return 5;
    if (sigprocmask(SIG_SETMASK, NULL, &old) < 0 || !sigismember(&old, SIGUSR1)) return 6;
    pid_t child = fork();
    if (child == 0) {
        sigset_t inherited;
        if (sigprocmask(SIG_SETMASK, NULL, &inherited) < 0
            || !sigismember(&inherited, SIGUSR1)) _exit(9);
        _exit(0);
    }
    int status;
    if (child < 0 || waitpid(child, &status, 0) != child || status != 0) return 7;
    NevaStartupHandleV1 process;
    if (neva_startup_find("process", &process) != NEVA_STATUS_OK) return 12;
    uint32_t parent_signal = sys_handle_dup(process.handle, HANDLE_RIGHT_RPC | HANDLE_RIGHT_SIGNAL);
    if (!parent_signal) return 13;
    child = fork();
    if (child == 0) {
        sys_sleep(20);
        NevaStatus sent = (NevaStatus)(int64_t)sys_rpc(parent_signal, PROCESS_RPC_SIGNAL, SIGUSR1, 0);
        _exit(sent == NEVA_STATUS_OK ? 0 : 22);
    }
    if (child < 0 || sigsuspend(&empty) != -1 || errno != EINTR || g_received != 2) return 14;
    (void)sys_handle_close(parent_signal);
    if (waitpid(child, &status, 0) != child || status != 0) return 15;
    child = fork();
    if (child == 0) {
        struct sigaction stop = { .sa_handler = SIG_DFL };
        sigset_t stop_mask;
        sigemptyset(&stop_mask);
        sigaddset(&stop_mask, SIGTSTP);
        if (sigaction(SIGTSTP, &stop, NULL) < 0
            || sigprocmask(SIG_BLOCK, &stop_mask, NULL) < 0
            || raise(SIGTSTP) < 0) _exit(23);
        // Exposing a pending stop in sigsuspend must stop, not terminate,
        // and SIGCONT alone must not complete the suspended signal wait.
        if (sigsuspend(&empty) != -1 || errno != EINTR || g_received != 3) _exit(24);
        if (sigprocmask(SIG_SETMASK, NULL, &stop_mask) < 0
            || !sigismember(&stop_mask, SIGTSTP)) _exit(25);
        _exit(0);
    }
    if (child < 0 || waitpid(child, &status, WUNTRACED) != child
        || !WIFSTOPPED(status) || WSTOPSIG(status) != SIGTSTP) return 16;
    if (kill(child, SIGCONT) < 0 || kill(child, SIGUSR1) < 0
        || waitpid(child, &status, 0) != child || status != 0) return 17;
    child = fork();
    if (child == 0) _exit(130);
    if (child < 0 || waitpid(child, &status, 0) != child
        || !WIFEXITED(status) || WEXITSTATUS(status) != 130) return 18;
    child = fork();
    if (child == 0) {
        signal(SIGINT, SIG_DFL);
        raise(SIGINT);
        _exit(26);
    }
    if (child < 0 || waitpid(child, &status, 0) != child
        || !WIFSIGNALED(status) || WTERMSIG(status) != SIGINT) return 19;
    if (sigprocmask(SIG_SETMASK, &empty, NULL) < 0) return 8;
    uint64_t epoch = sys_signal_epoch();
    int64_t created_event = sys_event_create(0, 1);
    if (created_event <= 0 || created_event > UINT32_MAX) return 28;
    uint32_t event = (uint32_t)created_event;
    if (raise(SIGUSR1) < 0
        || sys_event_wait_epoch(event, 0, epoch) != NEVA_STATUS_INTERRUPTED
        || sys_event_wait_epoch(event, 0, sys_signal_epoch()) != NEVA_STATUS_TIMED_OUT) return 28;
    (void)sys_handle_close(event);
    struct termios attributes, restored;
    uint32_t tty = neva_tty_handle();
    NevaStartupHandleV1 catalog;
    pid_t owner = tcgetpgrp(0);
    if (!tty || owner <= 0 || neva_startup_find("catalog", &catalog) != NEVA_STATUS_OK
        || sys_service_call(tty, PTY_SLAVE_RPC_SET_FOREGROUND,
               0, 0, 0, 0, NEVA_DEADLINE_INFINITE).status != NEVA_STATUS_ACCESS_DENIED
        || sys_service_call(catalog.handle, SESSION_CONTROL_RPC_SET_FOREGROUND,
               0, 0, 0, 0, NEVA_DEADLINE_INFINITE).status != NEVA_STATUS_ACCESS_DENIED
        || tcgetpgrp(0) != owner) return 22;
    if (tcgetattr(0, &attributes) < 0) return 10;
    restored = attributes;
    restored.c_cc[VERASE] = 8;
    if (tcsetattr(0, TCSANOW, &restored) < 0 || tcgetattr(0, &restored) < 0
        || restored.c_cc[VERASE] != 8 || tcsetattr(0, TCSANOW, &attributes) < 0) return 11;
    char formatted[20];
    if (snprintf(formatted, sizeof(formatted), "%*.*s:%d", -5, 3, "abcdef", 7) != 7
        || strcmp(formatted, "abc  :7") != 0) return 27;
    neva_println("D4_SIGNALS: query/mask/suspend/fork/termios PASS");
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc > 1 && strncmp(argv[1], "d5-", 3) == 0) return silt_check_exec(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "sessions")) return silt_check_sessions(argc - 1, argv + 1);
    int result = run_checks(argc, argv);
    if (result != 0) {
        neva_print("D4_SIGNALS: FAIL code=");
        neva_print_int((uint64_t)result);
        neva_putc('\n');
    }
    return result;
}
