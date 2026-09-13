#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

ssize_t silt_safe_read(int fd, void* buffer, size_t count) {
    ssize_t result;
    do { result = read(fd, buffer, count); } while (result < 0 && errno == EINTR);
    return result;
}

