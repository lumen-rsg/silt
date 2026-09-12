#include "runtime.h"
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/wait.h>

enum { READ, WRITE, EXECUTE, READ_ONLY, NON_EXECUTABLE, ILLEGAL, ALIGNMENT, FAULT_COUNT };
enum { DEFAULT, IGNORED, BLOCKED, CAUGHT_EXIT, NESTED, ESCAPE, POLICY_COUNT };
static const int g_signals[FAULT_COUNT] = {
    SIGSEGV, SIGSEGV, SIGSEGV, SIGSEGV, SIGSEGV, SIGILL, SIGBUS,
};
static volatile sig_atomic_t g_fault;
static volatile sig_atomic_t g_received;
static jmp_buf g_escape;

static void fault(int kind) {
    uintptr_t zero = 0;
    if (kind == READ) __asm__ volatile("ldr wzr, [%0]" : : "r"(zero) : "memory");
    if (kind == WRITE) __asm__ volatile("str wzr, [%0]" : : "r"(zero) : "memory");
    if (kind == EXECUTE) __asm__ volatile("blr %0" : : "r"(zero) : "x30", "memory");
    if (kind == READ_ONLY) {
        uintptr_t code = (uintptr_t)fault;
        __asm__ volatile("str wzr, [%0]" : : "r"(code) : "memory");
    }
    if (kind == NON_EXECUTABLE) {
        uint32_t code[4] = { 0xd65f03c0U, 0, 0, 0 }; // ret, on a writable NX stack
        __asm__ volatile("blr %0" : : "r"(code) : "x30", "memory");
    }
    if (kind == ILLEGAL) __asm__ volatile(".inst 0x00000000" : : : "memory");
    if (kind == ALIGNMENT) {
        uint64_t storage[2] = { 0, 0 };
        uintptr_t unaligned = (uintptr_t)storage + 1;
        uint64_t value;
        // Exclusive accesses require natural alignment even with SCTLR.A=0.
        __asm__ volatile("ldxr %0, [%1]" : "=&r"(value) : "r"(unaligned) : "memory");
    }
    _exit(99);
}

static void handler_exit(int sig) { _exit(64 + sig); }
static void handler_escape(int sig) { g_received = sig; longjmp(g_escape, 1); }
static void handler_fault(int sig) { (void)sig; fault(g_fault); }

static void child_probe(int kind, int policy) {
    int sig = g_signals[kind];
    sigset_t mask;
    sigemptyset(&mask);
    if (sigprocmask(SIG_SETMASK, &mask, NULL) < 0) _exit(90);
    struct sigaction action = { .sa_handler = SIG_DFL };
    if (policy == IGNORED) action.sa_handler = SIG_IGN;
    if (policy == BLOCKED || policy == CAUGHT_EXIT || policy == NESTED) action.sa_handler = handler_exit;
    if (policy == ESCAPE) action.sa_handler = handler_escape;
    if (sigaction(sig, &action, NULL) < 0) _exit(91);
    if (policy == BLOCKED) {
        sigaddset(&mask, sig);
        if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) _exit(92);
    }
    if (policy == NESTED) {
        g_fault = kind;
        if (signal(SIGUSR1, handler_fault) == SIG_ERR || raise(SIGUSR1) < 0) _exit(93);
        _exit(94);
    }
    if (policy == ESCAPE && setjmp(g_escape)) {
        sigset_t restored;
        if (g_received != sig || sigprocmask(SIG_SETMASK, NULL, &restored) < 0
            || sigismember(&restored, sig)) _exit(95);
        // The abandoned fault frame must not suppress subsequent delivery.
        if (signal(SIGUSR2, handler_exit) == SIG_ERR || raise(SIGUSR2) < 0) _exit(96);
        _exit(97);
    }
    fault(kind);
}

static int run_checks(void) {
    for (int kind = 0; kind < FAULT_COUNT; kind++) {
        for (int policy = 0; policy < POLICY_COUNT; policy++) {
            pid_t child = fork();
            if (child == 0) child_probe(kind, policy);
            int status = 0;
            // Exercise both retained Process and direct-child wait-any paths.
            pid_t selector = (policy & 1) ? -1 : child;
            if (child < 0 || waitpid(selector, &status, 0) != child) return 1;
            int normal = policy == CAUGHT_EXIT || policy == ESCAPE;
            int code = 64 + (policy == ESCAPE ? SIGUSR2 : g_signals[kind]);
            if (normal ? !WIFEXITED(status) || WIFSIGNALED(status) || WEXITSTATUS(status) != code
                       : !WIFSIGNALED(status) || WIFEXITED(status) || WTERMSIG(status) != g_signals[kind]) {
                neva_print("D4_FAULTS: kind="); neva_print_int(kind);
                neva_print(" policy="); neva_print_int(policy);
                neva_print(" status="); neva_print_int(status); neva_putc('\n');
                return 2;
            }
            errno = 0;
            if (waitpid(child, &status, WNOHANG) != -1 || errno != ECHILD) return 3;
        }
    }
    const int codes[] = { 1, 128 + SIGSEGV, 128 + SIGILL, 128 + SIGBUS };
    for (unsigned i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        pid_t child = fork();
        if (child == 0) _exit(codes[i]);
        int status;
        if (child < 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status)
            || WIFSIGNALED(status) || WEXITSTATUS(status) != codes[i]) return 4;
    }
    neva_println("D4_FAULTS: typed wait/default/ignore/block/catch/nested/escape PASS");
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc == 2) {
        if (!strcmp(argv[1], "segv")) child_probe(READ, DEFAULT);
        if (!strcmp(argv[1], "ill")) child_probe(ILLEGAL, DEFAULT);
        if (!strcmp(argv[1], "bus")) child_probe(ALIGNMENT, DEFAULT);
        if (!strcmp(argv[1], "exit139")) return 139;
        return 64;
    }
    int result = run_checks();
    if (result) { neva_print("D4_FAULTS: FAIL code="); neva_print_int(result); neva_putc('\n'); }
    return result;
}
