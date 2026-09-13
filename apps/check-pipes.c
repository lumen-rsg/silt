#include "runtime.h"
#include "byte_stream.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "coreutils-env.h"

static volatile sig_atomic_t g_sigpipe;
static void catch_pipe(int signal) { (void)signal; g_sigpipe++; }

static int waited(pid_t child, int signal) {
    int status;
    return child > 0 && waitpid(child, &status, 0) == child
        && (signal ? WIFSIGNALED(status) && WTERMSIG(status) == signal
                   : WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void die_without_libc(int how) {
    if (how == 0) {
        (void)raise(SIGKILL);
    } else if (how == 1) {
        __asm__ volatile("br xzr" : : : "memory");
    } else {
        sys_exit(0);
    }
    _exit(99);
}

static int writer_death(int how, int buffered) {
    int ends[2];
    if (pipe(ends) < 0) return 1;
    pid_t child = fork();
    if (child == 0) {
        close(ends[0]);
        if (buffered && write(ends[1], "tail", 4) != 4) _exit(10);
        sys_sleep(30);
        die_without_libc(how);
    }
    close(ends[1]);
    char bytes[8];
    if (child < 0 || (buffered && (read(ends[0], bytes, sizeof(bytes)) != 4
        || memcmp(bytes, "tail", 4))) || read(ends[0], bytes, 1) != 0) return 2;
    close(ends[0]);
    if (!waited(child, how == 0 ? SIGKILL : how == 1 ? SIGSEGV : 0)) return 3;
    return 0;
}

static int reader_death(void) {
    int ends[2];
    if (pipe(ends) < 0) return 1;
    pid_t child = fork();
    if (child == 0) {
        close(ends[1]);
        sys_sleep(50);
        die_without_libc(0);
    }
    close(ends[0]);
    char bytes[NEVA_BYTE_STREAM_IO_MAX];
    memset(bytes, 'x', sizeof(bytes));
    if (child < 0) return 2;
    for (unsigned i = 0; i < NEVA_BYTE_STREAM_CAPACITY / sizeof(bytes); i++) {
        if (write(ends[1], bytes, sizeof(bytes)) != sizeof(bytes)) return 3;
    }
    g_sigpipe = 0;
    struct sigaction action = { .sa_handler = catch_pipe };
    if (sigaction(SIGPIPE, &action, NULL) < 0) return 4;
    if (write(ends[1], bytes, 1) != -1 || errno != EPIPE || g_sigpipe != 1) return 5;
    close(ends[1]);
    return waited(child, SIGKILL) ? 0 : 6;
}

static int broadcast(int writing) {
    int ends[2];
    if (pipe(ends) < 0) return 1;
    char bytes[NEVA_BYTE_STREAM_IO_MAX];
    memset(bytes, 'b', sizeof(bytes));
    if (writing) {
        for (unsigned i = 0; i < NEVA_BYTE_STREAM_CAPACITY / sizeof(bytes); i++) {
            if (write(ends[1], bytes, sizeof(bytes)) != sizeof(bytes)) return 2;
        }
    }
    int64_t ready = sys_event_create(0, 2);
    if (ready <= 0) return 3;
    pid_t children[2];
    for (unsigned i = 0; i < 2; i++) {
        children[i] = fork();
        if (children[i] == 0) {
            close(ends[writing ? 0 : 1]);
            struct sigaction ignore = { .sa_handler = SIG_IGN };
            if (sigaction(SIGPIPE, &ignore, NULL) < 0) _exit(10);
            sys_event_signal((uint32_t)ready, 1);
            int result = writing ? write(ends[1], bytes, 1) : read(ends[0], bytes, 1);
            _exit(writing ? (result == -1 && errno == EPIPE ? 0 : 11) : (result == 0 ? 0 : 12));
        }
        if (children[i] < 0) return 4;
    }
    for (unsigned i = 0; i < 2; i++) {
        if (sys_event_wait((uint32_t)ready, NEVA_DEADLINE_INFINITE, 0) != NEVA_STATUS_OK) return 5;
    }
    sys_sleep(30);
    close(ends[0]);
    close(ends[1]);
    sys_handle_close((uint32_t)ready);
    return waited(children[0], 0) && waited(children[1], 0) ? 0 : 6;
}

static int exec_lifetime(int cloexec) {
    int ends[2];
    if (pipe(ends) < 0) return 1;
    pid_t child = fork();
    if (child == 0) {
        close(ends[0]);
        int duplicate = dup(ends[1]);
        if (duplicate < 0 || fcntl(ends[1], F_SETFD, FD_CLOEXEC) < 0) _exit(10);
        // Failed exec must leave the pipe and descriptor flags untouched.
        char* bad[] = { "missing", NULL };
        if (execve("/bin/missing", bad, NULL) != -1) _exit(11);
        close(ends[1]);
        if (dup2(duplicate, STDOUT_FILENO) != STDOUT_FILENO) _exit(12);
        close(duplicate);
        if (cloexec && fcntl(STDOUT_FILENO, F_SETFD, FD_CLOEXEC) < 0) _exit(13);
        char* args[] = { "check-pipes", cloexec ? "park" : "write", NULL };
        execve("/bin/check-pipes", args, NULL);
        _exit(14);
    }
    close(ends[1]);
    char bytes[16];
    if (child < 0) return 2;
    if (!cloexec && (read(ends[0], bytes, sizeof(bytes)) != 7 || memcmp(bytes, "exec-ok", 7))) return 3;
    if (read(ends[0], bytes, sizeof(bytes)) != 0) return 4;
    close(ends[0]);
    if (cloexec) {
        // EOF must arrive at exec commit, while the replacement is still alive.
        int status;
        if (waitpid(child, &status, WNOHANG) != 0 || kill(child, SIGKILL) < 0) return 5;
    }
    return waited(child, cloexec ? SIGKILL : 0) ? 0 : 6;
}

static int sigpipe_default(void) {
    int ends[2];
    if (pipe(ends) < 0) return 1;
    close(ends[0]);
    pid_t child = fork();
    if (child == 0) {
        struct sigaction action = { .sa_handler = SIG_DFL };
        sigaction(SIGPIPE, &action, NULL);
        (void)write(ends[1], "x", 1);
        _exit(10);
    }
    close(ends[1]);
    return waited(child, SIGPIPE) ? 0 : 2;
}

static int capability_lifetime(void) {
    uint32_t reader, writer;
    if (sys_byte_stream_create(&reader, &writer) != NEVA_STATUS_OK) return 1;
    // A readiness handle grants neither signal authority nor endpoint liveness.
    int64_t event = (int64_t)sys_rpc(reader, BYTE_STREAM_RPC_DUP_CHANGE_EVENT, 0, 0);
    if (event <= 0 || sys_event_signal((uint32_t)event, 1) == NEVA_STATUS_OK) return 2;
    uint32_t attenuated = sys_handle_dup(writer, HANDLE_RIGHT_RPC);
    char byte = 'c';
    if (!attenuated || (int64_t)sys_rpc(attenuated, BYTE_STREAM_RPC_WRITE, (uintptr_t)&byte, 1) >= 0) return 3;
    sys_handle_close(attenuated);
    // Retain only the child's inherited writer, then revoke it through the
    // parent's explicit endpoint capability. A blocked reader must observe EOF.
    pid_t child = fork();
    if (child == 0) {
        sys_handle_close(reader);
        sys_handle_close((uint32_t)event);
        sys_sleep(80);
        int64_t result = (int64_t)sys_rpc(writer, BYTE_STREAM_RPC_WRITE, (uintptr_t)&byte, 1);
        _exit(result < 0 ? 0 : 10);
    }
    if (child < 0 || sys_revoke(writer, (uint32_t)child) < 0) return 4;
    sys_handle_close(writer);
    if (sys_event_wait((uint32_t)event, NEVA_DEADLINE_INFINITE, 0) != NEVA_STATUS_OK
        || (int64_t)sys_rpc(reader, BYTE_STREAM_RPC_READ, (uintptr_t)&byte, 1) != 0) return 5;
    sys_handle_close(reader);
    sys_handle_close((uint32_t)event);
    return waited(child, 0) ? 0 : 6;
}

static int atomic_writers(void) {
    int ends[2];
    if (pipe(ends) < 0) return 1;
    pid_t children[2];
    for (unsigned i = 0; i < 2; i++) {
        children[i] = fork();
        if (children[i] == 0) {
            close(ends[0]);
            char packet[NEVA_BYTE_STREAM_IO_MAX];
            memset(packet, 'A' + (int)i, sizeof(packet));
            for (unsigned n = 0; n < 16; n++) {
                if (write(ends[1], packet, sizeof(packet)) != sizeof(packet)) _exit(10);
            }
            _exit(0);
        }
        if (children[i] < 0) return 2;
    }
    close(ends[1]);
    unsigned offset = 0;
    unsigned packets[2] = { 0, 0 };
    char tag = 0;
    char bytes[73];
    int count;
    while ((count = read(ends[0], bytes, sizeof(bytes))) > 0) {
        for (int i = 0; i < count; i++) {
            if (offset == 0) {
                tag = bytes[i];
                if (tag != 'A' && tag != 'B') return 3;
                packets[tag - 'A']++;
            }
            if (bytes[i] != tag) return 4;
            offset = (offset + 1) % NEVA_BYTE_STREAM_IO_MAX;
        }
    }
    close(ends[0]);
    if (count != 0 || offset || packets[0] != 16 || packets[1] != 16) return 5;
    return waited(children[0], 0) && waited(children[1], 0) ? 0 : 6;
}

static int allocation_rollback(void) {
    for (int round = 0; round < 4; round++) {
        int ends[16][2];
        unsigned count = 0;
        while (count < 16 && pipe(ends[count]) == 0) count++;
        if (!count || count == 16 || errno != EMFILE) return 1;
        for (unsigned i = 0; i < count; i++) {
            close(ends[i][0]);
            close(ends[i][1]);
        }
    }
    return 0;
}

static int suspended_death(void) {
    uint32_t reader, writer;
    if (sys_byte_stream_create(&reader, &writer) != NEVA_STATUS_OK) return 1;
    NevaForkResult child = sys_fork_capability_flags(NEVA_FORK_START_SUSPENDED);
    if (child.child_pid == 0) sys_exit(99);
    if (child.child_pid < 0 || !child.child_process_handle) return 2;
    sys_handle_close(writer);
    if ((NevaStatus)(int64_t)sys_rpc(child.child_process_handle, PROCESS_RPC_TERMINATE, 0, 0)
        != NEVA_STATUS_OK) return 3;
    NevaWaitResult result = sys_wait_capability(child.child_process_handle,
        WAIT_REPORT_EXITED, NEVA_DEADLINE_INFINITE);
    if (result.status != NEVA_STATUS_OK) return 4;
    char byte;
    if ((int64_t)sys_rpc(reader, BYTE_STREAM_RPC_READ, (uintptr_t)&byte, 1) != 0) return 5;
    sys_handle_close(reader);
    sys_handle_close(child.child_process_handle);
    return 0;
}

static int run_checks(void) {
    for (int how = 0; how < 3; how++) {
        if (writer_death(how, 1)) return 10 + how;
    }
    neva_println("D4_PIPES: kill/fault/raw-exit drain PASS");
    if (writer_death(0, 0) || reader_death()) return 20;
    neva_println("D4_PIPES: blocked EOF/EPIPE PASS");
    if (broadcast(0) || broadcast(1)) return 30;
    neva_println("D4_PIPES: reader/writer broadcast PASS");
    if (exec_lifetime(0) || exec_lifetime(1)) return 40;
    neva_println("D4_PIPES: dup/fork/exec/CLOEXEC PASS");
    if (sigpipe_default() || capability_lifetime()) return 50;
    neva_println("D4_PIPES: SIGPIPE/rights/revoke PASS");
    if (atomic_writers() || allocation_rollback() || suspended_death()) return 55;
    neva_println("D4_PIPES: atomic writes/exhaustion/suspended death PASS");
    for (int i = 0; i < 16; i++) {
        if (writer_death(0, 0)) return 60;
    }
    neva_println("D4_PIPES: repeated forced teardown PASS");
    return 0;
}

// Inspect binary pipeline/redirection output without the console's NUL filter.
static int dump_hex(void) {
    const char* hex = "0123456789abcdef";
    unsigned char buffer[64];
    ssize_t count;
    while ((count = read(0, buffer, sizeof(buffer))) != 0) {
        if (count < 0) { if (errno == EINTR) continue; return 1; }
        for (ssize_t index = 0; index < count; index++) {
            char pair[2] = {hex[buffer[index] >> 4], hex[buffer[index] & 15]};
            for (size_t done = 0; done < 2;) {
                ssize_t written = write(1, pair + done, 2 - done);
                if (written < 0 && errno == EINTR) continue;
                if (written <= 0) return 1;
                done += (size_t)written;
            }
        }
    }
    return write(1, "\n", 1) == 1 ? 0 : 1;
}

static int digest_input(void) {
    unsigned char buffer[512];
    uint64_t size = 0;
    uint64_t hash = UINT64_C(14695981039346656037);
    ssize_t count;
    while ((count = read(0, buffer, sizeof(buffer))) != 0) {
        if (count < 0) { if (errno == EINTR) continue; return 1; }
        size += (uint64_t)count;
        for (ssize_t i = 0; i < count; i++) hash = (hash ^ buffer[i]) * UINT64_C(1099511628211);
    }
    char output[64];
    int length = snprintf(output, sizeof(output), "%llu:%llx\n",
                          (unsigned long long)size, (unsigned long long)hash);
    return write(1, output, (size_t)length) == length ? 0 : 1;
}

static int stream_metadata(void) {
    struct stat reader, writer, file, alias, other;
    int ends[2];
    if (sizeof(ino_t) != 8 || sizeof(dev_t) != 8 || pipe(ends) < 0) return 1;
    if (fstat(ends[0], &reader) || fstat(ends[1], &writer)
        || !S_ISFIFO(reader.st_mode) || !S_ISFIFO(writer.st_mode)) return 2;
    if (close(ends[0]) || close(ends[1])) return 3;
    errno = 0;
    if (fstat(ends[0], &reader) != -1 || errno != EBADF) return 4;
    int fd = open("/bin/cat", O_RDONLY);
    if (fd < 0 || fstat(fd, &file) || close(fd)
        || stat("/bin/coreutils", &alias) || stat("/boot/c1/binary", &other)) return 5;
    if (!S_ISREG(file.st_mode) || file.st_ino == 0 || file.st_dev == 0
        || file.st_ino != alias.st_ino || file.st_dev != alias.st_dev
        || file.st_ino == other.st_ino || other.st_size != 20741) return 6;
    fd = open("/tmp/cu", O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0 || fstat(fd, &other) || close(fd) || !S_ISREG(other.st_mode)
        || other.st_ino <= UINT32_MAX || other.st_dev <= UINT32_MAX
        || file.st_dev == other.st_dev) return 7;
    if (stat("/tmp", &reader) || !S_ISDIR(reader.st_mode)
        || reader.st_dev != other.st_dev) return 9;
    if (allocation_rollback()) return 8;
    return 0;
}

static int cwd_contract(void) {
    char* original = getcwd(NULL, 0);
    if (!original || original[0] != '/') return 1;
    char sentinel = 'x';
    errno = 0;
    if (getcwd(&sentinel, 0) || errno != ERANGE || sentinel != 'x') return 2;
    errno = 0;
    if (getcwd(NULL, 1) || errno != ERANGE) return 3;
    if (chdir("/boot/c1/../c1//") != 0) return 4;
    char current[256];
    if (!getcwd(current, sizeof(current)) || strcmp(current, "/boot/c1")) return 5;
    if (chdir("/absent") != -1 || !getcwd(current, sizeof(current))
        || strcmp(current, "/boot/c1")) return 6;
    if (chdir(original)) return 7;
    free(original);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc >= 3 && strcmp(argv[1], "env") == 0)
        return coreutils_test_environment("/bin/printenv", argv + 2);
    if (argc == 2 && strcmp(argv[1], "cwd") == 0) return cwd_contract();
    if (argc == 2 && strcmp(argv[1], "metadata") == 0) return stream_metadata();
    if (argc == 2 && strcmp(argv[1], "digest") == 0) return digest_input();
    if (argc == 2 && strcmp(argv[1], "hex") == 0) return dump_hex();
    if (argc == 2 && strcmp(argv[1], "write") == 0) return write(1, "exec-ok", 7) == 7 ? 0 : 1;
    if (argc == 2 && strcmp(argv[1], "park") == 0) {
        for (;;) sys_sleep(1000);
    }
    int result = run_checks();
    if (result) {
        neva_print("D4_PIPES: FAIL code=");
        neva_print_int((uint64_t)result);
        neva_putc('\n');
    }
    return result;
}
