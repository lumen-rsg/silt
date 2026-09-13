#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// The private control file supplies a generation and successful-fork budget.
// This is a Linux reference fault injector, not an emulation of Neva quotas.
pid_t fork(void) {
    static pid_t (*original)(void);
    static unsigned long generation;
    static int remaining = -1;
    const char* control = getenv("D4_FORK_CONTROL");
    if (!original) original = (pid_t (*)(void))dlsym(RTLD_NEXT, "fork");
    if (!original || !control) { errno = EIO; return -1; }
    char bytes[80];
    int fd;
    do { fd = open(control, O_RDONLY | O_CLOEXEC); } while (fd < 0 && errno == EINTR);
    if (fd < 0) { errno = EIO; return -1; }
    ssize_t count;
    do { count = read(fd, bytes, sizeof(bytes) - 1); } while (count < 0 && errno == EINTR);
    close(fd);
    unsigned long next;
    int budget;
    if (count <= 0) { errno = EIO; return -1; }
    bytes[count] = 0;
    if (sscanf(bytes, "%lu %d", &next, &budget) != 2) { errno = EIO; return -1; }
    if (next != generation) {
        generation = next;
        remaining = budget;
    }
    if (!remaining) {
        static const char marker[] = "RX_INJECTED_FORK\n";
        size_t offset = 0;
        while (offset < sizeof(marker) - 1) {
            ssize_t written = write(2, marker + offset, sizeof(marker) - 1 - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) break;
            offset += (size_t)written;
        }
        errno = EAGAIN;
        return -1;
    }
    if (remaining > 0) remaining--;
    return original();
}

// Match Silt's COW fork implementation of this Dash exec optimization.
pid_t vfork(void) {
    return fork();
}
