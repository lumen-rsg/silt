// Inject SIGINT after Dash's pending check, just before its blocking read.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static ssize_t (*real_read)(int, void *, size_t);
static pid_t owner;
static unsigned char previous;

__attribute__((constructor)) static void initialize(void) {
    *(void **)(&real_read) = dlsym(RTLD_NEXT, "read");
    owner = getpid();
    if (!real_read) _exit(125);
}

ssize_t read(int fd, void *buffer, size_t size) {
    const char *control = getenv("D4_READ_SIGNAL_CONTROL");
    if (fd == STDIN_FILENO && size && getpid() == owner && control) {
        int saved_errno = errno;
        int input;
        do { input = open(control, O_RDONLY | O_CLOEXEC); } while (input < 0 && errno == EINTR);
        unsigned char generation = previous;
        ssize_t count = -1;
        if (input >= 0) {
            do { count = real_read(input, &generation, 1); } while (count < 0 && errno == EINTR);
            close(input);
        }
        errno = saved_errno;
        if (count == 1 && generation != previous) {
            previous = generation;
            static const char marker[] = "D4_READ_SIGNAL_INJECTED\n";
            size_t written = 0;
            while (written < sizeof(marker) - 1) {
                ssize_t result = write(STDERR_FILENO, marker + written,
                                       sizeof(marker) - 1 - written);
                if (result < 0 && errno == EINTR) continue;
                if (result <= 0) _exit(125);
                written += (size_t)result;
            }
            raise(SIGINT);
        }
    }
    return real_read(fd, buffer, size);
}
