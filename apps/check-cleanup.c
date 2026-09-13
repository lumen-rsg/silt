#include "runtime.h"
#include "../libc/cleanup.h"
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <silt_pipeline.h>
#include <stdio.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <termios.h>

#define ROUNDS 160
static jmp_buf g_escape;
static volatile sig_atomic_t g_jumping;
static volatile sig_atomic_t g_force;
static volatile sig_atomic_t g_seen;
static volatile sig_atomic_t g_nested_ok;
static uint32_t g_process;
static uint32_t g_inherited;

// Keep this handler alone on a complete RX page. Registration must not depend
// on some unrelated function having brought its instruction page into RAM.
extern void cleanup_cold_handler(int sig);
__asm__(".pushsection .text.cleanup_cold, \"ax\"\n"
        ".balign 4096\n.global cleanup_cold_handler\n"
        ".type cleanup_cold_handler, %function\ncleanup_cold_handler:\n"
        "mov x0, #0\nmov x8, #4\nsvc #0\nb .\n"
        ".size cleanup_cold_handler, .-cleanup_cold_handler\n"
        ".balign 4096\n.popsection\n");

static int capacity(void) {
    uint32_t handles[128];
    int count = 0;
    while (count < 128) {
        uint32_t handle = sys_handle_dup(g_process, HANDLE_RIGHT_RPC | HANDLE_RIGHT_INSPECT);
        if (!handle) break;
        handles[count++] = handle;
    }
    for (int i = 0; i < count; i++) (void)sys_handle_close(handles[i]);
    return count;
}

static void interrupt(int sig) {
    (void)sig;
    if (!g_force && (!g_silt_cleanup_head || !g_silt_cleanup_head->handle)) return;
    g_seen++;
    if (g_jumping) longjmp(g_escape, 1);
    // A jump contained inside the handler must preserve the interrupted
    // operation's guard and active signal frame.
    jmp_buf local;
    SiltCleanup* outer = g_silt_cleanup_head;
    if (setjmp(local) == 0) {
        SiltCleanup inner;
        uint32_t mask = silt_cleanup_begin(&inner);
        uint32_t handle = sys_handle_dup(g_process, HANDLE_RIGHT_RPC | HANDLE_RIGHT_INSPECT);
        if (!silt_cleanup_adopt(&inner, handle)) _exit(80);
        silt_cleanup_ready(mask);
        longjmp(local, 7);
    }
    sigset_t blocked;
    if (g_silt_cleanup_head == outer && sigprocmask(SIG_SETMASK, NULL, &blocked) == 0
        && sigismember(&blocked, SIGUSR1)) g_nested_ok++;
}

static int ownership_checks(void) {
    int baseline = capacity();
    g_jumping = 1;
    g_force = 1;
    for (volatile int i = 0; i < ROUNDS; i++) {
        if (setjmp(g_escape) == 0) {
            SiltCleanup outer, inner;
            uint32_t mask = silt_cleanup_begin(&outer);
            uint32_t handle = sys_handle_dup(g_process, HANDLE_RIGHT_RPC | HANDLE_RIGHT_INSPECT);
            if (!silt_cleanup_adopt(&outer, handle)) return 1;
            silt_cleanup_ready(mask);
            mask = silt_cleanup_begin(&inner);
            handle = sys_handle_dup(g_process, HANDLE_RIGHT_RPC | HANDLE_RIGHT_INSPECT);
            // Force the acquisition/adoption window: delivery must stay
            // pending until the new handle is registered for unwinding.
            if (!handle || raise(SIGUSR1) < 0) return 2;
            if (!silt_cleanup_adopt(&inner, handle)
                || sys_handle_get_flags(handle) != HANDLE_FLAG_CLOEXEC) return 3;
            silt_cleanup_ready(mask);
            return 4;
        }
        if (g_silt_cleanup_head || capacity() != baseline) return 5;
    }
    // Ordinary longjmp restores ownership but must not invent a saved mask.
    sigset_t mask, current;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR2);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) return 6;
    if (!setjmp(g_escape)) {
        SiltCleanup cleanup;
        uint32_t old = silt_cleanup_begin(&cleanup);
        if (!silt_cleanup_adopt(&cleanup,
                sys_handle_dup(g_process, HANDLE_RIGHT_RPC | HANDLE_RIGHT_INSPECT))) return 7;
        silt_cleanup_ready(old);
        longjmp(g_escape, 0);
    }
    if (g_silt_cleanup_head || capacity() != baseline
        || sigprocmask(SIG_SETMASK, NULL, &current) < 0 || !sigismember(&current, SIGUSR2)) return 8;
    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) < 0) return 9;
    uint32_t occupied[128];
    volatile int count = 0;
    while (count < 128) {
        uint32_t handle = sys_handle_dup(g_process, HANDLE_RIGHT_RPC | HANDLE_RIGHT_INSPECT);
        if (!handle) break;
        occupied[count++] = handle;
    }
    if (!setjmp(g_escape)) {
        SiltCleanup empty;
        uint32_t old = silt_cleanup_begin(&empty);
        if (silt_cleanup_adopt(&empty,
                sys_handle_dup(g_process, HANDLE_RIGHT_RPC | HANDLE_RIGHT_INSPECT))) return 32;
        if (raise(SIGUSR1) < 0) return 33;
        silt_cleanup_ready(old);
        return 34;
    }
    for (int i = 0; i < count; i++) {
        if (sys_handle_get_flags(occupied[i]) < 0) return 35;
        sys_handle_close(occupied[i]);
    }
    if (g_silt_cleanup_head || capacity() != baseline) return 36;
    g_force = 0;
    neva_println("D4_CLEANUP: acquisition/nested guards/mask/CLOEXEC PASS");
    return 0;
}

static int fork_exec_checks(void) {
    pid_t owner = getpid();
    if (setjmp(g_escape)) {
        _exit(getpid() != owner && !g_silt_cleanup_head
            && sys_handle_get_flags(g_inherited) < 0 ? 0 : 40);
    }
    SiltCleanup cleanup;
    uint32_t mask = silt_cleanup_begin(&cleanup);
    g_inherited = silt_cleanup_adopt(&cleanup,
        sys_handle_dup(g_process, HANDLE_RIGHT_RPC | HANDLE_RIGHT_INSPECT));
    silt_cleanup_ready(mask);
    if (!g_inherited) return 41;
    pid_t child = fork();
    if (!child) longjmp(g_escape, 1);
    int status;
    if (child < 0 || waitpid(child, &status, 0) != child || status
        || g_silt_cleanup_head != &cleanup || sys_handle_get_flags(g_inherited) != HANDLE_FLAG_CLOEXEC) return 42;
    silt_cleanup_end(&cleanup);

    child = fork();
    if (!child) {
        uint32_t old = silt_cleanup_begin(&cleanup);
        uint32_t temporary = silt_cleanup_adopt(&cleanup,
            sys_handle_dup(g_process, HANDLE_RIGHT_RPC | HANDLE_RIGHT_INSPECT));
        silt_cleanup_ready(old);
        uint32_t retained = sys_handle_dup(g_process, HANDLE_RIGHT_RPC | HANDLE_RIGHT_INSPECT);
        if (!temporary || !retained) _exit(43);
        char a[16], b[16];
        snprintf(a, sizeof(a), "%u", temporary);
        snprintf(b, sizeof(b), "%u", retained);
        char* args[] = { "check-cleanup", "exec-check", a, b, NULL };
        if (execve("/bin/absent-cleanup-test", args, NULL) != -1
            || g_silt_cleanup_head != &cleanup || sys_handle_get_flags(temporary) != HANDLE_FLAG_CLOEXEC) _exit(44);
        execve("/bin/check-cleanup", args, NULL);
        _exit(45);
    }
    if (child < 0 || waitpid(child, &status, 0) != child || status) return 46;
    neva_println("D4_CLEANUP: fork isolation/failed exec/exec close PASS");
    return 0;
}

static void ipc_interrupt(int sig) {
    (void)sig;
    g_seen++;
}

static int ipc_reply_checks(void) {
    uint32_t endpoint = sys_ipc_endpoint_create();
    if (!endpoint) return 50;
    for (int i = 0; i < 16; i++) {
        pid_t child = fork();
        if (!child) {
            g_seen = 0;
            if (signal(SIGUSR1, ipc_interrupt) == SIG_ERR) _exit(51);
            uint64_t reply = sys_ipc_call(endpoint, 1, 0, 0, 0);
            _exit(reply == 0x12345678 && g_seen == 1 ? 0 : 52);
        }
        if (child < 0) return 53;
        IpcMessage message;
        if (sys_ipc_recv(&message) != (uint32_t)child || kill(child, SIGUSR1) < 0) return 54;
        // Receiving proves admission; the caught signal must stay pending
        // until this exact reply completes, never expose call input registers.
        sys_sleep(5);
        if (sys_ipc_reply(0x12345678, 0) < 0) return 55;
        int status;
        if (waitpid(child, &status, 0) != child || status) return 56;
    }
    pid_t child = fork();
    if (!child) {
        if (signal(SIGUSR2, SIG_DFL) == SIG_ERR) _exit(57);
        (void)sys_ipc_call(endpoint, 1, 0, 0, 0);
        _exit(58);
    }
    IpcMessage message;
    if (child < 0 || sys_ipc_recv(&message) != (uint32_t)child || kill(child, SIGUSR2) < 0) return 59;
    // No reply: default-fatal delivery must tear down the admitted call.
    int status;
    if (waitpid(child, &status, 0) != child || !WIFSIGNALED(status) || WTERMSIG(status) != SIGUSR2) return 60;
    sys_handle_close(endpoint);
    neva_println("D4_CLEANUP: caught reply/default-fatal IPC ownership PASS");
    return 0;
}

static int wait_checks(int operation, int jumping) {
    int ends[2] = { -1, -1 };
    char bytes[512];
    memset(bytes, 'x', sizeof(bytes));
    if (operation < 2 && pipe(ends) < 0) return 10;
    if (operation == 1) {
        for (int i = 0; i < 8; i++) if (write(ends[1], bytes, sizeof(bytes)) != sizeof(bytes)) return 11;
    }
    int64_t start = sys_event_create(0, 1), ack = sys_event_create(0, 1);
    uint32_t parent = sys_handle_dup(g_process, HANDLE_RIGHT_RPC | HANDLE_RIGHT_SIGNAL);
    if (start <= 0 || ack <= 0 || !parent) return 12;
    pid_t helper = fork();
    if (!helper) {
        for (int i = 0; i < ROUNDS; i++) {
            if (sys_event_wait((uint32_t)start, NEVA_DEADLINE_INFINITE, 0) != NEVA_STATUS_OK) _exit(81);
            int acknowledged = 0;
            // Timing does not establish ownership: the handler only escapes
            // when it sees a registered handle. Retry until the parent acks.
            for (int retry = 0; retry < 2000; retry++) {
                sys_sleep(1);
                if (sys_event_wait((uint32_t)ack, 0, 0) == NEVA_STATUS_OK) { acknowledged = 1; break; }
                if ((NevaStatus)(int64_t)sys_rpc(parent, PROCESS_RPC_SIGNAL, SIGUSR1, 0)
                    != NEVA_STATUS_OK) _exit(82);
            }
            if (!acknowledged) _exit(83);
        }
        _exit(0);
    }
    if (helper < 0) return 13;
    int baseline = capacity();
    int result = 0;
    g_jumping = jumping;
    g_seen = 0;
    g_nested_ok = 0;
    for (volatile int i = 0; i < ROUNDS; i++) {
        int previous = g_seen;
        if (!setjmp(g_escape)) {
            sys_event_signal((uint32_t)start, 1);
            for (;;) {
                int count = operation == 1 ? write(ends[1], bytes, 1)
                    : read(operation == 2 ? 0 : ends[0], bytes, 1);
                if (count != -1 || errno != EINTR) {
                    int io_errno = errno;
                    (void)sys_signal_mask(0, UINT32_MAX);
                    neva_print("D4_CLEANUP: unexpected I/O op="); neva_print_int(operation);
                    neva_print(" jump="); neva_print_int(jumping);
                    neva_print(" round="); neva_print_int(i);
                    neva_print(" result="); neva_print_int((uint64_t)(int64_t)count);
                    neva_print(" byte="); neva_print_int((unsigned char)bytes[0]);
                    neva_print(" errno="); neva_print_int(io_errno); neva_putc('\n');
                    result = 14; goto done;
                }
                if (!jumping && g_seen > previous) break;
            }
        }
        sys_event_signal((uint32_t)ack, 1);
        if (g_seen <= previous || g_silt_cleanup_head || capacity() != baseline) {
            result = 15; goto done;
        }
    }
    if (!jumping && g_nested_ok != g_seen) result = 16;
done:
    if (result) kill(helper, SIGKILL);
    int status;
    if (waitpid(helper, &status, 0) != helper || (!result && status != 0)) result = 17;
    sys_handle_close(parent);
    sys_handle_close((uint32_t)start);
    sys_handle_close((uint32_t)ack);
    if (operation == 1 && read(ends[0], bytes, sizeof(bytes)) != sizeof(bytes)) result = 18;
    if (operation == 0 && (write(ends[1], "z", 1) != 1 || read(ends[0], bytes, 1) != 1
        || bytes[0] != 'z')) result = 19;
    if (ends[0] >= 0) { close(ends[0]); close(ends[1]); }
    return result;
}

static int background_child(int operation) {
    g_jumping = 1;
    g_force = 1;
    if (signal(SIGTTOU, interrupt) == SIG_ERR) return 20;
    struct termios before, after;
    if (tcgetattr(0, &before) < 0) return 21;
    after = before;
    after.c_cc[VERASE] = before.c_cc[VERASE] == 8 ? 127 : 8;
    pid_t group = getpgrp();
    int baseline = capacity();
    for (volatile int i = 0; i < ROUNDS; i++) {
        if (!setjmp(g_escape)) {
            if (operation == 0) tcsetattr(0, TCSANOW, &after);
            if (operation == 1) write(1, ".", 1);
            if (operation == 2) tcsetpgrp(0, group);
            return 22;
        }
        if (g_silt_cleanup_head || capacity() != baseline) return 23;
    }
    if (tcgetattr(0, &after) < 0 || after.c_cc[VERASE] != before.c_cc[VERASE]
        || tcgetpgrp(0) == group) return 24;
    return 0;
}

static int background_checks(void) {
    struct termios before, mode;
    if (tcgetattr(0, &before) < 0) return 25;
    mode = before;
    mode.c_lflag |= TOSTOP;
    if (tcsetattr(0, TCSANOW, &mode) < 0) return 26;
    int result = 0;
    for (int operation = 0; operation < 3; operation++) {
        silt_job_prepare(0, 0);
        pid_t child = fork();
        if (!child) _exit(background_child(operation));
        if (child < 0) { result = 27; break; }
        silt_job_finish(child);
        int status;
        if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) {
            neva_print("D4_CLEANUP: background status="); neva_print_int(status); neva_putc('\n');
            result = 28; break;
        }
    }
    if (tcsetattr(0, TCSANOW, &before) < 0) result = 29;
    return result;
}

static uint32_t handle_number(const char* text) {
    uint64_t value = 0;
    for (unsigned i = 0; text[i]; i++) {
        if (i == 10 || text[i] < '0' || text[i] > '9') return 0;
        value = value * 10 + (unsigned)(text[i] - '0');
        if (value > UINT32_MAX) return 0;
    }
    return (uint32_t)value;
}

static int resource_quota_checks(void) {
    int baseline = capacity();
    for (int round = 0; round < 4; round++) {
        pid_t children[6];
        int count = 0;
        int result = 0;
        for (; count < 6; count++) {
            pid_t child = fork();
            if (!child) {
                for (;;) sys_sleep(1000);
            }
            if (child < 0) { result = 70; break; }
            children[count] = child;
        }
        for (int attempt = 0; !result && attempt < 8; attempt++) {
            errno = 0;
            pid_t child = fork();
            if (!child) _exit(71);
            if (child >= 0) {
                (void)waitpid(child, NULL, 0);
                result = 72;
            } else if (errno != EAGAIN) {
                result = 73;
            }
        }
        for (int index = 0; index < count; index++) {
            if (kill(children[index], SIGKILL) < 0) result = 74;
        }
        for (int index = 0; index < count; index++) {
            int status = 0;
            if (waitpid(children[index], &status, 0) != children[index]
                || !WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) result = 75;
        }
        if (capacity() != baseline) result = 76;
        if (result) return result;
    }
    neva_println("D4_RESOURCE: quota EAGAIN/reap/refill/handle capacity PASS");
    return 0;
}

static int descriptor_resource_checks(void) {
    int baseline = capacity();
    for (int round = 0; round < 4; round++) {
        // PrivateTmp currently has one file slot, shared with earlier cases.
        const char* existing = "/tmp/dash.out";
        char absent[64];
        snprintf(absent, sizeof(absent), "/tmp/d4-fd-absent-%d-%d", getpid(), round);
        int file = open(existing, O_RDWR | O_CREAT | O_TRUNC, 0600);
        if (file < 0 || write(file, "keep", 4) != 4) {
            neva_print("D4_DESCRIPTOR_SETUP: fd=");
            neva_print_int(file);
            neva_print(" errno=");
            neva_print_int(errno);
            neva_putc('\n');
            return 100;
        }
        int held[32], count = 0;
        while (count < 32) {
            int fd = dup(STDOUT_FILENO);
            if (fd < 0) break;
            held[count++] = fd;
        }
        if (!count || errno != EMFILE) return 101;
        if (open(existing, O_WRONLY | O_TRUNC) != -1 || errno != EMFILE) return 102;
        char bytes[4];
        if (lseek(file, 0, SEEK_SET) != 0 || read(file, bytes, 4) != 4
            || memcmp(bytes, "keep", 4)) return 103;
        if (open(absent, O_WRONLY | O_CREAT, 0600) != -1 || errno != EMFILE) return 104;
        if (openat(AT_FDCWD, existing, O_WRONLY | O_TRUNC) != -1 || errno != EMFILE) return 105;
        for (int slots = 0; slots <= 1; slots++) {
            if (slots && close(held[--count]) < 0) return 106;
            int handles_before = capacity();
            for (int attempt = 0; attempt < 8; attempt++) {
                int ends[2] = { -7, -9 };
                if (pipe(ends) != -1 || errno != EMFILE || ends[0] != -7 || ends[1] != -9) return 107;
            }
            if (capacity() != handles_before) return 108;
        }
        // The second released descriptor must admit both endpoints again.
        if (close(held[--count]) < 0) return 109;
        int ends[2];
        if (pipe(ends) < 0 || write(ends[1], "P", 1) != 1 || read(ends[0], bytes, 1) != 1
            || bytes[0] != 'P' || close(ends[1]) < 0 || read(ends[0], bytes, 1) != 0
            || close(ends[0]) < 0) return 110;
        for (int i = 0; i < count; i++) if (close(held[i]) < 0) return 111;
        if (lseek(file, 0, SEEK_SET) != 0 || read(file, bytes, 4) != 4
            || memcmp(bytes, "keep", 4) || close(file) < 0) return 112;
        struct stat info;
        if (stat(absent, &info) != -1 || errno != ENOENT) return 113;
        if (capacity() != baseline) return 114;
    }
    neva_println("D4_DESCRIPTORS: rejected open preserves files/pipe EMFILE/refill PASS");
    for (int command = 0; command < 2; command++) {
        int operation = command ? F_DUPFD_CLOEXEC : F_DUPFD;
        if (fcntl(1, operation, -1) != -1 || errno != EINVAL
            || fcntl(1, operation, 32) != -1 || errno != EINVAL) return 115;
        int fd = fcntl(1, operation, 31);
        if (fd != 31 || fcntl(fd, F_GETFD) != (command ? FD_CLOEXEC : 0)
            || fcntl(1, operation, 31) != -1 || errno != EMFILE || close(fd) < 0) return 116;
    }
    // Fill independent descriptions, not just aliases to stdout.
    int files[32], count = 0;
    while (count < 32) {
        int fd = open("/dev/null", O_RDWR);
        if (fd < 0) break;
        files[count++] = fd;
    }
    if (!count || errno != EMFILE) return 117;
    for (int i = 0; i < count; i++) if (close(files[i]) < 0) return 118;
    if (capacity() != baseline) return 119;
    neva_println("D4_DESCRIPTORS: F_DUPFD bounds/flags/description refill PASS");
    return 0;
}

static int descriptor_shell(void) {
    // Leave fd 10 for Dash's CLOEXEC job-control terminal. The interactive
    // cases occupy and release 3..9 themselves, using real descriptor limits.
    for (int fd = 11; fd < 32; fd++) if (dup2(STDOUT_FILENO, fd) != fd) return 120;
    char* args[] = { "dash", "-i", NULL };
    execve("/bin/dash", args, environ);
    return 121;
}

int main(int argc, char* argv[]) {
    if (argc == 4 && !strcmp(argv[1], "exec-check")) {
        uint32_t temporary = handle_number(argv[2]);
        uint32_t retained = handle_number(argv[3]);
        int result = !temporary || !retained || g_silt_cleanup_head || sys_handle_get_flags(temporary) >= 0
            || sys_handle_get_flags(retained) < 0;
        sys_handle_close(retained);
        return result ? 47 : 0;
    }
    NevaStartupHandleV1 process;
    if (neva_startup_find("process", &process) != NEVA_STATUS_OK) return 30;
    g_process = process.handle;
    if (argc == 2 && !strcmp(argv[1], "fd-shell")) return descriptor_shell();
    if (argc == 2 && !strcmp(argv[1], "copy")) {
        char bytes[64];
        ssize_t count;
        while ((count = read(0, bytes, sizeof(bytes))) > 0) {
            if (write(1, bytes, (size_t)count) != count) return 122;
        }
        return count < 0 ? 123 : 0;
    }
    if (argc == 2 && !strcmp(argv[1], "descriptors")) {
        int result = descriptor_resource_checks();
        if (result) {
            neva_print("D4_DESCRIPTORS: FAIL code=");
            neva_print_int(result);
            neva_putc('\n');
        }
        return result;
    }
    if (argc == 2 && !strcmp(argv[1], "quota")) {
        int result = resource_quota_checks();
        if (result) {
            neva_print("D4_RESOURCE: FAIL code=");
            neva_print_int(result);
            neva_putc('\n');
        }
        return result;
    }
    if (argc == 2 && !strcmp(argv[1], "fds")) {
        int count = 0;
        for (int fd = 0; fd < 32; fd++) if (fcntl(fd, F_GETFD) >= 0) count++;
        neva_print("RX_FDS=");
        neva_print_int(count);
        neva_putc('\n');
        return 0;
    }
    pid_t cold = fork();
    if (!cold) {
        if (signal(SIGUSR2, cleanup_cold_handler) == SIG_ERR) _exit(48);
        raise(SIGUSR2);
        _exit(49);
    }
    int cold_status;
    if (cold < 0 || waitpid(cold, &cold_status, 0) != cold || cold_status) {
        neva_println("D4_CLEANUP: cold handler registration FAIL");
        return 48;
    }
    neva_println("D4_CLEANUP: nonresident signal handler PASS");
    if (signal(SIGUSR1, interrupt) == SIG_ERR) return 31;
    int result = ownership_checks();
    if (!result) result = fork_exec_checks();
    if (!result) result = ipc_reply_checks();
    for (int operation = 0; !result && operation < 3; operation++) {
        for (int jumping = 0; !result && jumping < 2; jumping++) result = wait_checks(operation, jumping);
    }
    if (!result) neva_println("D4_CLEANUP: pipe/tty EINTR/longjmp capacity PASS");
    if (!result) result = background_checks();
    if (!result) neva_println("D4_CLEANUP: background write/attributes/foreground capacity PASS");
    if (result) { neva_print("D4_CLEANUP: FAIL code="); neva_print_int(result); neva_putc('\n'); }
    return result;
}
