#define _XOPEN_SOURCE 700
#ifndef SILT_REFERENCE_HOST
#include "runtime.h"
#endif
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef SILT_REFERENCE_HOST
#include <stdint.h>
#include <time.h>
#include <sys/ioctl.h>
static void neva_print(const char* text) { fputs(text, stdout); }
static void neva_println(const char* text) { puts(text); }
static void neva_print_int(int value) { printf("%d", value); }
static void neva_putc(char value) { putchar(value); }
static void sys_sleep(unsigned milliseconds) {
    struct timespec interval = { milliseconds / 1000, (milliseconds % 1000) * 1000000L };
    while (nanosleep(&interval, &interval) && errno == EINTR) {}
}
#endif

extern char** environ;

static int send_value(int fd, int value) { return write(fd, &value, sizeof(value)) == sizeof(value); }
static int receive_value(int fd, int* value) { return read(fd, value, sizeof(*value)) == sizeof(*value); }
static int reap(pid_t child) {
    int status = -1;
    int ok = waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!ok) { neva_print("D4_SESSIONS: child status="); neva_print_int(status); neva_putc('\n'); }
    return ok;
}

static int sessions(void) {
    pid_t initial_sid = getsid(0), initial_group = getpgrp();
    if (initial_sid <= 0 || initial_group != getpid()) return 1;
    if (kill(getpid(), 0) || kill(0, 0) || kill(-initial_group, 0)) return 90;
    if (kill(2147483647, 0) != -1 || errno != ESRCH) return 91;
    errno = 0;
    if (setsid() != -1 || errno != EPERM) return 2;
    int ready[2], go[2];
    if (pipe(ready) || pipe(go)) return 3;
    pid_t child = fork();
    if (child == 0) {
        close(ready[0]); close(go[1]);
        if (getpgrp() != initial_group || getsid(0) != initial_sid) _exit(10);
        if (setsid() != getpid() || getsid(0) != getpid() || getpgrp() != getpid()) _exit(11);
        if (getsid(getppid()) != initial_sid || getpgid(getppid()) != initial_group) _exit(64);
        errno = 0;
        if (setsid() != -1 || errno != EPERM) _exit(12);
        if (setpgid(0, 0) != -1 || errno != EPERM) _exit(13);
        if (open("/dev/tty", O_RDWR) != -1 || errno != ENXIO) _exit(60);
        if (tcgetpgrp(0) != -1 || errno != ENOTTY) _exit(61);
        if (tcsetpgrp(0, getpgrp()) != -1 || errno != ENOTTY) _exit(62);
        struct termios attributes;
        if (!isatty(0) || tcgetattr(0, &attributes) || tcsetattr(0, TCSANOW, &attributes)) _exit(63);
        if (!send_value(ready[1], getpid())) _exit(14);
        int value;
        if (!receive_value(go[0], &value)) _exit(15);
        pid_t leader = getpid();
        pid_t grandchild = fork();
        if (grandchild == 0) {
            if (getsid(0) != leader || getpgrp() != leader) _exit(16);
            if (setpgid(0, 0) || getpgrp() != getpid() || getsid(0) != leader) _exit(17);
            if (setsid() != -1 || errno != EPERM) _exit(18);
            _exit(0);
        }
        _exit(grandchild < 0 || !reap(grandchild) ? 19 : 0);
    }
    close(ready[1]); close(go[0]);
    int value;
    if (child < 0 || !receive_value(ready[0], &value) || value != child) return 4;
    if (getsid(child) != child || getpgid(child) != child || kill(child, 0)) return 5;
    if (setpgid(child, initial_group) != -1 || errno != EPERM) return 6;
    if (!send_value(go[1], 1) || !reap(child)) return 7;
    close(ready[0]); close(go[1]);
    if (getsid(0) != initial_sid || getpgrp() != initial_group) return 8;
    if (getsid(child) != -1 || errno != ESRCH) return 9;
    neva_println("D4_SESSIONS: new session/inheritance/leader/cross-session PASS");
    return 0;
}

static int regroup(void) {
    int ready[2], go[2];
    if (pipe(ready) || pipe(go)) return 20;
    pid_t child = fork();
    if (child == 0) {
        close(ready[0]); close(go[1]);
        int value;
        if (!receive_value(go[0], &value) || getpgrp() != getpid()) _exit(30);
        // A failed exec must not revoke the parent's setpgid authority.
        char* arguments[] = { "missing", NULL };
        execve("/bin/does-not-exist", arguments, environ);
        if (!send_value(ready[1], getpgrp()) || !receive_value(go[0], &value)) _exit(31);
        _exit(getpgrp() == value ? 0 : 32);
    }
    close(ready[1]); close(go[0]);
    if (child < 0 || setpgid(child, 0) || getpgid(child) != child) return 21;
    if (!send_value(go[1], 1)) return 22;
    int value;
    if (!receive_value(ready[0], &value) || value != child) return 23;
    if (setpgid(child, getpgrp()) || getpgid(child) != getpgrp()) return 24;
    if (!send_value(go[1], getpgrp()) || !reap(child)) return 25;
    close(ready[0]); close(go[1]);
    neva_println("D4_SESSIONS: parent regroup/failed exec/actual queries PASS");
    return 0;
}

static int executed(void) {
    int ready[2], go[2];
    if (pipe(ready) || pipe(go)) return 40;
    pid_t child = fork();
    if (child == 0) {
        close(ready[0]); close(go[1]);
        char fd_ready[16], fd_go[16];
        snprintf(fd_ready, sizeof(fd_ready), "%d", ready[1]);
        snprintf(fd_go, sizeof(fd_go), "%d", go[0]);
        char* arguments[] = { "check-signals", "sessions", "exec", fd_ready, fd_go, NULL };
        char* environment[] = { "PATH=/bin", "D4_INHERIT=owned", NULL };
        execve("/bin/check-signals", arguments, environment);
        _exit(50);
    }
    close(ready[1]); close(go[0]);
    int value;
    if (child < 0 || !receive_value(ready[0], &value) || value != child) return 41;
    if (setpgid(child, 0) != -1 || errno != EACCES) return 42;
    if (!send_value(go[1], 1) || !reap(child)) return 43;
    close(ready[0]); close(go[1]);
    neva_println("D4_SESSIONS: committed exec revokes parent regroup PASS");
    return 0;
}

static volatile sig_atomic_t orphan_hup, orphan_cont;
static void orphan_signal(int number) {
    if (number == SIGHUP) orphan_hup++;
    if (number == SIGCONT) orphan_cont++;
}

static int orphan_terminal(void) {
    struct termios saved, changed;
    if (tcgetattr(0, &saved)) return 80;
    changed = saved;
    changed.c_lflag |= TOSTOP;
    if (signal(SIGTTOU, SIG_IGN) == SIG_ERR || tcsetattr(0, TCSANOW, &changed)) return 81;
    if (signal(SIGTTOU, SIG_DFL) == SIG_ERR) return 82;
    char byte;
    int result = 0;
    if (read(0, &byte, 1) != -1 || errno != EIO) result = 83;
    if (!result && (write(1, "X", 1) != -1 || errno != EIO)) result = 84;
    if (!result && (tcsetattr(0, TCSANOW, &saved) != -1 || errno != EIO)) result = 85;
    if (!result && (tcsetpgrp(0, getpgrp()) != -1 || errno != EIO)) result = 86;
    if (signal(SIGTTOU, SIG_IGN) == SIG_ERR || tcsetattr(0, TCSANOW, &saved)) return 87;
    if (signal(SIGTTOU, SIG_DFL) == SIG_ERR) return 88;
    // These default actions must be discarded, while SIGSTOP above was honored.
    if (kill(getpid(), SIGTSTP) || kill(getpid(), SIGTTIN) || kill(getpid(), SIGTTOU)) return 89;
    return result;
}

static int orphaned(int parent_exits) {
    int result_pipe[2], resume_pipe[2];
    if (pipe(result_pipe) || pipe(resume_pipe)) return 70;
    pid_t parent = fork();
    if (parent == 0) {
        close(result_pipe[0]);
        pid_t child = fork();
        if (child == 0) {
            if (setpgid(0, 0) || signal(SIGHUP, orphan_signal) == SIG_ERR
                || signal(SIGCONT, orphan_signal) == SIG_ERR) _exit(71);
            close(resume_pipe[1]);
            if (kill(getpid(), SIGSTOP)) _exit(72);
            int value;
            if (parent_exits == 2) {
                int received;
                do received = receive_value(resume_pipe[0], &value); while (!received && errno == EINTR);
                if (!received) _exit(92);
            }
            int result = orphan_hup == 1 && (parent_exits == 2 ? orphan_cont >= 1 : orphan_cont == 1)
                ? orphan_terminal() : 73;
            if (!send_value(result_pipe[1], result)) _exit(74);
            _exit(result);
        }
        int status;
        if (child < 0 || waitpid(child, &status, WUNTRACED) != child
            || !WIFSTOPPED(status) || WSTOPSIG(status) != SIGSTOP) _exit(75);
        close(resume_pipe[0]);
        if (parent_exits == 1) _exit(0);
        if (parent_exits == 2) {
            // The first STOP was consumed, so an odd number of retained
            // transitions can fill all eight normal slots while stopped.
            if (kill(child, SIGCONT)) _exit(93);
            for (int cycle = 0; cycle < 3; cycle++) {
                if (kill(child, SIGSTOP) || kill(child, SIGCONT)) _exit(94);
            }
            if (kill(child, SIGSTOP)) _exit(95);
        }
        if (setsid() != getpid()) _exit(76);
        if (parent_exits == 2) {
            if (!send_value(resume_pipe[1], 1)) _exit(96);
            int stops = 0, continues = 0;
            for (int report = 0; report < 9; report++) {
                if (waitpid(child, &status, WUNTRACED | WCONTINUED) != child) _exit(97);
                stops += WIFSTOPPED(status);
                continues += WIFCONTINUED(status);
            }
            if (stops != 4 || continues != 5) _exit(98);
        }
        if (!reap(child)) _exit(76);
        _exit(0);
    }
    close(result_pipe[1]); close(resume_pipe[0]); close(resume_pipe[1]);
    int result;
    if (parent < 0 || !receive_value(result_pipe[0], &result)) return 77;
    close(result_pipe[0]);
    if (result) return result;
    if (!reap(parent)) return 78;
    neva_println(parent_exits == 2 ? "D4_SESSIONS: full report queue/orphan mandatory continuation PASS"
                : parent_exits ? "D4_SESSIONS: parent exit/orphan HUP CONT/terminal EIO PASS"
                             : "D4_SESSIONS: parent setsid/orphan HUP CONT/terminal EIO PASS");
    return 0;
}

static int pty_basics(void) {
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master < 0) return 100;
    char path[40];
    char* name = ptsname(master);
    if (!name) return 101;
    strcpy(path, name);
    if (open(path, O_RDWR | O_NOCTTY) != -1 || errno != EACCES) return 102;
    if (grantpt(master) || open(path, O_RDWR | O_NOCTTY) != -1 || errno != EACCES) return 103;
    struct stat metadata;
    if (stat(path, &metadata) || !S_ISCHR(metadata.st_mode) || metadata.st_uid != getuid()
        || (metadata.st_mode & 0777) != 0620) return 121;
    if (unlockpt(master)) return 104;
    int slave = open(path, O_RDWR | O_NOCTTY);
    if (slave < 0 || !isatty(slave) || tcgetpgrp(slave) != -1 || errno != ENOTTY) return 105;
    if (fstat(slave, &metadata) || !S_ISCHR(metadata.st_mode) || metadata.st_uid != getuid()
        || (metadata.st_mode & 0777) != 0620) return 122;
#ifndef SILT_REFERENCE_HOST
    // Exercise provider attenuation directly, bypassing libc's fd-mode check.
    extern uint32_t silt_descriptor_tty(int descriptor);
    int read_only = open(path, O_RDONLY | O_NOCTTY);
    int write_only = open(path, O_WRONLY | O_NOCTTY);
    if (read_only < 0 || write_only < 0) return 123;
    char denied_byte = 'X';
    NevaStatus denied_read = sys_service_call(silt_descriptor_tty(write_only),
        PTY_SLAVE_RPC_READ, 0, 0, 0, 0, NEVA_DEADLINE_INFINITE).status;
    NevaStatus denied_write = sys_service_call(silt_descriptor_tty(read_only),
        PTY_SLAVE_RPC_WRITE, (uintptr_t)&denied_byte, 0, 0, 1, NEVA_DEADLINE_INFINITE).status;
    if (denied_read != NEVA_STATUS_ACCESS_DENIED || denied_write != NEVA_STATUS_ACCESS_DENIED) return 124;
    close(read_only); close(write_only);
#endif
    struct termios attributes;
    if (tcgetattr(slave, &attributes)) return 106;
    attributes.c_lflag &= ~ECHO;
    if (tcsetattr(slave, TCSANOW, &attributes)) return 107;
    char bytes[32];
    if (read(master, bytes, sizeof(bytes)) != -1 || errno != EAGAIN) return 108;
    // Descriptor ownership survives well beyond the legacy GUI heartbeat lease.
    sys_sleep(1000);
    if (write(master, "hello\n", 6) != 6 || read(slave, bytes, sizeof(bytes)) != 6
        || memcmp(bytes, "hello\n", 6)) return 109;
    if (write(slave, "output", 6) != 6 || read(master, bytes, sizeof(bytes)) != 6
        || memcmp(bytes, "output", 6)) return 110;
    int duplicate = dup(master);
    if (duplicate < 0 || close(master) || write(slave, "dup", 3) != 3
        || read(duplicate, bytes, sizeof(bytes)) != 3 || memcmp(bytes, "dup", 3)) return 111;
    int ready[2], go[2];
    if (pipe(ready) || pipe(go)) return 112;
    pid_t child = fork();
    if (child == 0) {
        close(ready[0]); close(go[1]); close(slave);
        if (fcntl(duplicate, F_SETFL, 0) || !send_value(ready[1], 1)) _exit(113);
        int value;
        if (!receive_value(go[0], &value) || write(duplicate, "fork\n", 5) != 5) _exit(114);
        _exit(0);
    }
    close(ready[1]); close(go[0]);
    int value;
    if (child < 0 || !receive_value(ready[0], &value) || (fcntl(duplicate, F_GETFL) & O_NONBLOCK)) return 115;
    if (close(duplicate) || !send_value(go[1], 1)) return 116;
    if (read(slave, bytes, sizeof(bytes)) != 5 || memcmp(bytes, "fork\n", 5) || !reap(child)) return 117;
    close(ready[0]); close(go[1]);
    // The child closed the final master at exit: queued input drained, then EOF.
    if (read(slave, bytes, sizeof(bytes)) != 0 || write(slave, "X", 1) != -1 || errno != EIO) return 118;
    close(slave);
    if (open(path, O_RDWR | O_NOCTTY) != -1 || errno != ENOENT
        || stat(path, &metadata) != -1 || errno != ENOENT) return 119;
    for (int i = 0; i < 12; i++) {
        int next = posix_openpt(O_RDWR | O_NOCTTY);
        if (next < 0 || !ptsname(next) || !strcmp(ptsname(next), path)) return 120;
        close(next);
    }
    neva_println("D4_SESSIONS: PTY grant/unlock/idle/I O/dup/fork/last close/reuse PASS");
    return 0;
}

static uint64_t now_ms(void) {
#ifdef SILT_REFERENCE_HOST
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
#else
    NevaStartupHandleV1 system;
    if (neva_startup_find("system", &system) != NEVA_STATUS_OK) return 0;
    return sys_rpc(system.handle, SYSTEM_RPC_UPTIME, 0, 0);
#endif
}

// Linux defers the master-to-line-discipline transfer. Wait for the input
// queue before testing a flush; Silt's service reply acknowledges that transfer.
static int input_queued(int slave) {
#ifdef SILT_REFERENCE_HOST
    uint64_t deadline = now_ms() + 1000;
    int available = 0;
    do {
        if (ioctl(slave, FIONREAD, &available)) return 0;
        if (available) return 1;
        sys_sleep(1);
    } while (now_ms() < deadline);
    return 0;
#else
    (void)slave;
    return 1;
#endif
}

static volatile sig_atomic_t interrupted;
static void interrupt_read(int number) { (void)number; interrupted++; }

static int pty_modes(void) {
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master < 0 || grantpt(master) || unlockpt(master)) return 190;
    int slave = open(ptsname(master), O_RDWR | O_NOCTTY);
    struct termios attributes;
    if (slave < 0 || tcgetattr(slave, &attributes)) return 191;
    attributes.c_lflag &= ~(ECHO | ISIG);
    if (tcsetattr(slave, TCSANOW, &attributes)) return 192;
    char bytes[32];
    // Empty EOF is an ordered record, not a persistent end-of-file state.
    char input[] = { 'a', 'b', 4, 4, 'c', '\n' };
    if (write(master, input, sizeof(input)) != sizeof(input)
        || read(slave, bytes, sizeof(bytes)) != 2 || memcmp(bytes, "ab", 2)
        || read(slave, bytes, sizeof(bytes)) != 0
        || read(slave, bytes, sizeof(bytes)) != 2 || memcmp(bytes, "c\n", 2)) return 193;
    attributes.c_lflag &= ~ICANON;
    attributes.c_cc[VMIN] = attributes.c_cc[VTIME] = 0;
    if (tcsetattr(slave, TCSANOW, &attributes) || read(slave, bytes, sizeof(bytes)) != 0) return 194;
    if (write(master, "raw", 3) != 3 || read(slave, bytes, sizeof(bytes)) != 3
        || memcmp(bytes, "raw", 3)) return 195;
    attributes.c_cc[VTIME] = 1;
    if (tcsetattr(slave, TCSANOW, &attributes)) return 196;
    uint64_t started = now_ms();
    if (read(slave, bytes, sizeof(bytes)) != 0 || now_ms() - started < 70) return 197;
    // VMIN+VTIME starts its timer only after the first byte and returns partial input.
    attributes.c_cc[VMIN] = 3;
    if (tcsetattr(slave, TCSANOW, &attributes) || write(master, "x", 1) != 1) return 198;
    started = now_ms();
    if (read(slave, bytes, sizeof(bytes)) != 1 || bytes[0] != 'x'
        || now_ms() - started < 70) return 199;
    attributes.c_cc[VTIME] = 0;
    if (tcsetattr(slave, TCSANOW, &attributes)) return 200;
    pid_t writer = fork();
    if (writer == 0) {
        sys_sleep(30);
        if (write(master, "1", 1) != 1) _exit(201);
        sys_sleep(30);
        _exit(write(master, "23", 2) == 2 ? 0 : 202);
    }
    if (writer < 0 || read(slave, bytes, sizeof(bytes)) != 3
        || memcmp(bytes, "123", 3) || !reap(writer)) return 203;
    // Descriptor status overrides all four blocking MIN/TIME modes.
    if (fcntl(slave, F_SETFL, O_NONBLOCK) || read(slave, bytes, sizeof(bytes)) != -1
        || errno != EAGAIN || fcntl(slave, F_SETFL, 0)) return 204;
    if (write(master, "discard", 7) != 7 || !input_queued(slave) || tcflush(slave, TCIFLUSH)) return 205;
    attributes.c_cc[VMIN] = 0;
    if (tcsetattr(slave, TCSANOW, &attributes) || read(slave, bytes, sizeof(bytes)) != 0) return 206;
    if (write(slave, "discard", 7) != 7 || tcflush(slave, TCOFLUSH)
        || read(master, bytes, sizeof(bytes)) != -1 || errno != EAGAIN) return 207;
    if (write(master, "discard", 7) != 7 || !input_queued(slave) || tcsetattr(slave, TCSAFLUSH, &attributes)
        || read(slave, bytes, sizeof(bytes)) != 0) return 208;
    if (write(slave, "drain", 5) != 5) return 209;
    pid_t reader = fork();
    if (reader == 0) {
        sys_sleep(60);
        _exit(read(master, bytes, sizeof(bytes)) == 5 && !memcmp(bytes, "drain", 5) ? 0 : 210);
    }
    started = now_ms();
    if (reader < 0 || tcdrain(slave) || !reap(reader)) return 211;
    // A non-child sender interrupts a read; a consumed byte must survive EINTR.
    struct sigaction action = { .sa_handler = interrupt_read };
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGUSR1, &action, NULL)) return 212;
    attributes.c_cc[VMIN] = 3;
    if (tcsetattr(slave, TCSANOW, &attributes)) return 213;
    for (int partial = 0; partial < 2; partial++) {
        if (partial && write(master, "p", 1) != 1) return 214;
        pid_t parent = getpid();
        pid_t sender = fork();
        if (sender == 0) { sys_sleep(60); _exit(kill(parent, SIGUSR1) ? 215 : 0); }
        int count = read(slave, bytes, sizeof(bytes));
        if (sender < 0 || (partial ? count != 1 || bytes[0] != 'p' : count != -1 || errno != EINTR)
            || !reap(sender) || interrupted != partial + 1) return 216;
    }
    if (signal(SIGUSR1, SIG_DFL) == SIG_ERR) return 217;
    attributes.c_cc[VMIN] = attributes.c_cc[VTIME] = 0;
    attributes.c_iflag = ICRNL | ISTRIP;
    if (tcsetattr(slave, TCSANOW, &attributes) || write(master, "\r\n\341", 3) != 3
        || read(slave, bytes, sizeof(bytes)) != 3 || memcmp(bytes, "\n\na", 3)) return 235;
    attributes.c_iflag = INLCR | IGNCR;
    if (tcsetattr(slave, TCSANOW, &attributes) || write(master, "\r\n", 2) != 2
        || read(slave, bytes, sizeof(bytes)) != 1 || bytes[0] != '\r') return 236;
    attributes.c_iflag = 0;
    attributes.c_oflag = 0;
    attributes.c_lflag = ICANON | ECHONL;
    if (tcsetattr(slave, TCSANOW, &attributes) || write(master, "\r\n", 2) != 2
        || read(slave, bytes, sizeof(bytes)) != 2 || memcmp(bytes, "\r\n", 2)
        || read(master, bytes, sizeof(bytes)) != 1 || bytes[0] != '\n') return 237;
    attributes.c_lflag = ICANON | ECHO | ECHOK;
    if (tcsetattr(slave, TCSANOW, &attributes)) return 238;
    char full_line[255]; memset(full_line, 'k', sizeof(full_line));
    if (write(master, full_line, sizeof(full_line)) != sizeof(full_line)
        || write(master, &attributes.c_cc[VKILL], 1) != 1
        || write(master, "z\n", 2) != 2
        || read(slave, bytes, sizeof(bytes)) != 2 || memcmp(bytes, "z\n", 2)) return 239;
    if (close(slave) || close(master)) return 240;
    neva_println("D4_SESSIONS: PTY canonical EOF/MIN TIME/nonblock/flush/drain/EINTR PASS");
    return 0;
}

static int retired_foreground(int slave) {
#ifdef SILT_REFERENCE_HOST
    (void)slave;
    return 0;
#else
    extern uint32_t silt_descriptor_tty(int descriptor);
    NevaStartupHandleV1 self;
    if (neva_startup_find("process", &self) != NEVA_STATUS_OK) return 244;
    int ready[2], go[2];
    if (pipe(ready) || pipe(go)) return 245;
    pid_t peer = fork();
    if (peer == 0) {
        close(ready[0]); close(go[1]);
        int value;
        if (setpgid(0, 0) || !send_value(ready[1], 1) || !receive_value(go[0], &value)) _exit(246);
        _exit(0);
    }
    close(ready[1]); close(go[0]);
    int value;
    if (peer < 0 || !receive_value(ready[0], &value)) return 247;
    int64_t retained = (int64_t)sys_rpc(self.handle, PROCESS_RPC_DUP_GROUP, peer, 0);
    int64_t own = (int64_t)sys_rpc(self.handle, PROCESS_RPC_DUP_GROUP, 0, 0);
    if (retained <= 0 || own <= 0 || !send_value(go[1], 1) || !reap(peer)) return 248;
    NevaProcessGroupInfoV1 info;
    if ((NevaStatus)(int64_t)sys_rpc((uint32_t)retained, PROCESS_GROUP_RPC_QUERY,
        (uintptr_t)&info, sizeof(info)) != NEVA_STATUS_OK || info.member_count) return 249;
    NevaTtyForegroundClientV1 request = {
        .magic = NEVA_TTY_CONTROL_MAGIC, .version = NEVA_TTY_ABI_VERSION, .size = sizeof(request),
        .caller_group_handle = (uint32_t)own, .target_group_handle = (uint32_t)retained,
        .target_generation = info.generation, .session_id = info.session_id,
    };
    NevaStatus status = sys_service_call_pair(silt_descriptor_tty(slave),
        PTY_SLAVE_RPC_SET_FOREGROUND_CLIENT, (uintptr_t)&request, 0,
        (uint32_t)own, (uint32_t)retained, sizeof(request), NEVA_DEADLINE_INFINITE).status;
    sys_handle_close((uint32_t)own); sys_handle_close((uint32_t)retained);
    close(ready[0]); close(go[1]);
    return status == NEVA_STATUS_ACCESS_DENIED && tcgetpgrp(slave) == getpid() ? 0 : 250;
#endif
}

static int pty_session(void) {
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    char path[40];
    if (master < 0 || !ptsname(master) || grantpt(master) || unlockpt(master)) return 130;
    strcpy(path, ptsname(master));
    int ready[2], go[2];
    if (pipe(ready) || pipe(go)) return 131;
    pid_t child = fork();
    if (child == 0) {
        close(ready[0]); close(go[1]); close(master);
        if (setsid() != getpid()) _exit(132);
        int detached = open(path, O_RDWR | O_NOCTTY);
        if (detached < 0 || open("/dev/tty", O_RDWR) != -1 || errno != ENXIO) _exit(133);
        close(detached);
        pid_t nonleader = fork();
        if (nonleader == 0) {
            int ordinary = open(path, O_RDWR);
            _exit(ordinary < 0 || tcgetpgrp(ordinary) != -1 || errno != ENOTTY
                || open("/dev/tty", O_RDWR) != -1 || errno != ENXIO ? 165 : 0);
        }
        if (nonleader < 0 || !reap(nonleader)) _exit(166);
        int full[32], used = 0;
        while (used < 32 && (full[used] = dup(0)) >= 0) used++;
        if (used == 32 || errno != EMFILE || open(path, O_RDWR) != -1 || errno != EMFILE) _exit(167);
        while (used) close(full[--used]);
        if (open("/dev/tty", O_RDWR) != -1 || errno != ENXIO) _exit(168);
        int slave = open(path, O_RDWR);
        int controlling = open("/dev/tty", O_RDWR);
        if (slave < 0) _exit(160);
        if (controlling < 0) _exit(161);
        if (tcgetpgrp(slave) != getpid()) _exit(162);
        if (tcgetpgrp(controlling) != getpid()) _exit(163);
        if (tcsetpgrp(slave, getpgrp())) { neva_print("D4_PTY errno="); neva_print_int(errno); _exit(164); }
        int peer_ready[2], peer_go[2];
        if (pipe(peer_ready) || pipe(peer_go)) _exit(241);
        pid_t peer = fork();
        if (peer == 0) {
            close(peer_ready[0]); close(peer_go[1]);
            int value;
            if (setsid() != getpid() || !send_value(peer_ready[1], 1)
                || !receive_value(peer_go[0], &value)) _exit(242);
            _exit(0);
        }
        close(peer_ready[1]); close(peer_go[0]);
        int peer_value;
        if (peer < 0 || !receive_value(peer_ready[0], &peer_value)
            || tcsetpgrp(slave, peer) != -1 || errno != EPERM
            || tcgetpgrp(slave) != getpid() || !send_value(peer_go[1], 1) || !reap(peer)) _exit(243);
        close(peer_ready[0]); close(peer_go[1]);
        int retired = retired_foreground(slave);
        if (retired) _exit(retired);
        int second = posix_openpt(O_RDWR | O_NOCTTY);
        if (second < 0 || grantpt(second) || unlockpt(second) || !ptsname(second)) _exit(135);
        int ordinary = open(ptsname(second), O_RDWR);
        if (ordinary < 0 || tcgetpgrp(ordinary) != -1 || errno != ENOTTY
            || tcgetpgrp(controlling) != getpid()) _exit(136);
        close(ordinary); close(second);
        if (dup2(slave, 0) < 0 || dup2(slave, 1) < 0) _exit(137);
        close(slave); close(controlling);
        int temporary = open(path, O_RDWR | O_CLOEXEC);
        if (temporary < 0 || !(fcntl(temporary, F_GETFD) & FD_CLOEXEC)) _exit(169);
        char out[16], in[16], closed[16];
        snprintf(out, sizeof(out), "%d", ready[1]); snprintf(in, sizeof(in), "%d", go[0]);
        snprintf(closed, sizeof(closed), "%d", temporary);
        char* arguments[] = { "check-signals", "sessions", "pty-exec", out, in, closed, NULL };
        execve("/bin/check-signals", arguments, environ);
        _exit(138);
    }
    close(ready[1]); close(go[0]);
    int value;
    if (child < 0) return 139;
    if (!receive_value(ready[0], &value) || value != child) { (void)reap(child); return 139; }
    int stopped;
    if (waitpid(child, &stopped, WUNTRACED) != child || !WIFSTOPPED(stopped)) return 157;
    int foreign = open(path, O_RDWR);
    if (foreign < 0 || tcgetpgrp(foreign) != -1 || errno != ENOTTY) return 140;
    close(foreign);
    close(master);
    if (!send_value(go[1], 1) || !receive_value(ready[0], &value) || value != 0 || !reap(child)) return 141;
    close(ready[0]); close(go[1]);
    neva_println("D4_SESSIONS: PTY O_NOCTTY/acquisition/one per session/exec/hangup PASS");
    return 0;
}

static int pty_leader_exit(void) {
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master < 0 || grantpt(master) || unlockpt(master)) return 220;
    char path[40]; strcpy(path, ptsname(master));
    int first = open(path, O_RDWR | O_NOCTTY);
    char byte;
    if (first < 0 || close(first) || read(master, &byte, 1) != -1 || errno != EIO) return 221;
    int reopened = open(path, O_RDWR | O_NOCTTY);
    if (reopened < 0 || read(master, &byte, 1) != -1 || errno != EAGAIN) return 222;
    close(reopened);
    int result[2];
    if (pipe(result)) return 223;
    pid_t leader = fork();
    if (leader == 0) {
        close(result[0]); close(master);
        if (setsid() != getpid()) _exit(224);
        int slave = open(path, O_RDWR);
        int ready[2];
        if (slave < 0 || pipe(ready)) _exit(225);
        pid_t foreground = fork();
        if (foreground == 0) {
            close(ready[0]);
            orphan_hup = 0;
            if (setpgid(0, 0) || signal(SIGHUP, orphan_signal) == SIG_ERR
                || !send_value(ready[1], getpid())) _exit(226);
            uint64_t deadline = now_ms() + 3000;
            while (!orphan_hup && now_ms() < deadline) sys_sleep(1);
            int status = orphan_hup != 1 || tcgetpgrp(slave) != -1 || errno != ENOTTY
                || open("/dev/tty", O_RDWR) != -1 || errno != ENXIO;
            if (!send_value(result[1], status)) _exit(227);
            _exit(status);
        }
        close(ready[1]);
        int value;
        if (foreground < 0 || !receive_value(ready[0], &value) || value != foreground
            || tcsetpgrp(slave, foreground)) _exit(228);
        _exit(0);
    }
    close(result[1]);
    int value;
    if (leader < 0 || !receive_value(result[0], &value) || value || !reap(leader)) return 229;
    close(result[0]);
    // A later eligible leader can acquire the detached, still-live device.
    leader = fork();
    if (leader == 0) {
        close(master);
        if (setsid() != getpid()) _exit(230);
        int slave = open(path, O_RDWR);
        _exit(slave < 0 || tcgetpgrp(slave) != getpid() ? 231 : 0);
    }
    if (leader < 0 || !reap(leader) || close(master)) return 232;
    int pool[16], count = 0;
    while (count < 16 && (pool[count] = posix_openpt(O_RDWR | O_NOCTTY)) >= 0) count++;
    if (!count || count == 16 || errno != ENOSPC) return 233;
    while (count) close(pool[--count]);
    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || close(master)) return 234;
    neva_println("D4_SESSIONS: PTY slave reopen/leader exit/HUP/reacquire/pool recovery PASS");
    return 0;
}

static int pty_exec(int ready, int go) {
    orphan_hup = orphan_cont = 0;
    if (signal(SIGHUP, orphan_signal) == SIG_ERR || signal(SIGCONT, orphan_signal) == SIG_ERR
        || tcgetpgrp(0) != getpid() || tcgetsid(0) != getsid(0)) return 150;
    int tty = open("/dev/tty", O_RDWR);
    if (tty < 0 || tcgetpgrp(tty) != getpid()) return 151;
    close(tty);
    if (!send_value(ready, getpid()) || kill(getpid(), SIGSTOP)) return 152;
    int value;
    if (!receive_value(go, &value)) return 153;
    // Read blocks until the provider observes final master closure; a caught
    // hangup may interrupt that wait before its permanent EOF is visible.
    char byte;
    int result;
    do result = read(0, &byte, 1); while (result < 0 && errno == EINTR);
    if (result != 0 || orphan_hup != 1 || orphan_cont != 1 || open("/dev/tty", O_RDWR) != -1 || errno != ENXIO) return 154;
    return send_value(ready, 0) ? 0 : 155;
}

static int pty_expect(int master, const char* expected) {
    char seen[2048];
    size_t used = 0;
    uint64_t deadline = now_ms() + 10000U;
    while (now_ms() < deadline) {
        int count = read(master, seen + used, sizeof(seen) - used - 1);
        if (count > 0) {
            used += (size_t)count; seen[used] = 0;
            if (strstr(seen, expected)) return 1;
            if (used > sizeof(seen) / 2) {
                memmove(seen, seen + used / 2, used - used / 2); used -= used / 2;
            }
        } else if (count < 0 && errno != EAGAIN && errno != EINTR) break;
        sys_sleep(1);
    }
    seen[used] = 0;
    neva_print("D4_PTY expected="); neva_println(expected);
    neva_println(seen);
    return 0;
}

static int pty_dash(void) {
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master < 0 || grantpt(master) || unlockpt(master) || !ptsname(master)) return 170;
    char path[40]; strcpy(path, ptsname(master));
    pid_t child = fork();
    if (child == 0) {
        close(master);
        if (setsid() != getpid()) _exit(171);
        int slave = open(path, O_RDWR);
        if (slave < 0 || dup2(slave, 0) < 0 || dup2(slave, 1) < 0 || dup2(slave, 2) < 0) _exit(172);
        close(slave);
        char* arguments[] = { "dash", "-i", NULL };
        char* environment[] = { "PATH=/bin", "PS1=P4> ", "TERM=dumb", NULL };
        execve("/bin/dash", arguments, environment);
        _exit(173);
    }
    if (child < 0 || !pty_expect(master, "P4> ")) return 174;
    const char* command = "printf 'D4_PTY_READY\\n'\n";
    if (write(master, command, strlen(command)) != (int)strlen(command)
        || !pty_expect(master, "D4_PTY_READY\n")) return 175;
    if (write(master, "check-cleanup copy\n", 19) != 19) return 176;
    // The input line appears twice: terminal echo and the foreground copy program.
    if (write(master, "D4_INPUT\n", 9) != 9 || !pty_expect(master, "D4_INPUT\nD4_INPUT\n")) return 177;
    if (write(master, "\032", 1) != 1 || !pty_expect(master, "P4> ")) return 178;
    if (write(master, "jobs\n", 5) != 5 || !pty_expect(master, "Stopped")) return 179;
    if (write(master, "fg\n", 3) != 3 || !pty_expect(master, "check-cleanup copy")) return 180;
    if (write(master, "D4_RESUME\n", 10) != 10 || !pty_expect(master, "D4_RESUME\nD4_RESUME\n")) return 181;
    if (write(master, "\003", 1) != 1 || !pty_expect(master, "P4> ")) return 182;
    if (write(master, "exit 0\n", 7) != 7 || !reap(child)) return 183;
    close(master);
    neva_println("D4_SESSIONS: real Dash over PTY/input/stop/jobs/fg/interrupt/exit PASS");
    return 0;
}

#ifdef SILT_REFERENCE_HOST
int main(int argc, char** argv) {
#else
int silt_check_sessions(int argc, char** argv) {
#endif
#ifdef SILT_REFERENCE_HOST
    int reference = pty_modes();
    if (reference) fprintf(stderr, "reference failure: %d\n", reference);
    return reference;
#endif
    if (argc == 2 && !strcmp(argv[1], "pty-dash")) {
        int result = pty_dash();
        if (result) { neva_print("D4_PTY_DASH: FAIL code="); neva_print_int(result); neva_putc('\n'); }
        return result;
    }
    if (argc == 5 && !strcmp(argv[1], "pty-exec")) {
        if (fcntl(atoi(argv[4]), F_GETFD) != -1 || errno != EBADF) return 156;
        return pty_exec(atoi(argv[2]), atoi(argv[3]));
    }
    if (argc == 4 && !strcmp(argv[1], "exec")) {
        pid_t child = fork();
        if (child == 0) {
            _exit(!environ[0] || strcmp(environ[0], "PATH=/bin")
                || !environ[1] || strcmp(environ[1], "D4_INHERIT=owned") || environ[2]);
        }
        if (child < 0 || !reap(child)) return 51;
        int value;
        return !send_value(atoi(argv[2]), getpid()) || !receive_value(atoi(argv[3]), &value);
    }
    int result = sessions();
    if (!result) result = regroup();
    if (!result) result = executed();
    if (!result) result = orphaned(0);
    if (!result) result = orphaned(1);
    if (!result) result = orphaned(2);
    if (!result) result = pty_basics();
    if (!result) result = pty_modes();
    if (!result) result = pty_session();
    if (!result) result = pty_leader_exit();
    if (result) {
        neva_print("D4_SESSIONS: FAIL code="); neva_print_int(result); neva_putc('\n');
    }
    return result;
}
