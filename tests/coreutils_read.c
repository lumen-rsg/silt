#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
static int mode, calls;
static ssize_t transport_read(int fd, void* buffer, size_t count) {
    (void)fd;
    calls++;
    if (mode == 1 && calls < 4) { errno = EINTR; return -1; }
    if (mode == 2) { errno = EIO; return -1; }
    if (mode == 3) return 0;
    if (count > 2) count = 2;
    memcpy(buffer, "ok", count);
    return (ssize_t)count;
}
#define read transport_read
#include "../ports/coreutils/read.c"
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
int main(void) {
    char buffer[8];
    mode = 1;
    CHECK(silt_safe_read(0, buffer, sizeof(buffer)) == 2);
    CHECK(calls == 4 && !memcmp(buffer, "ok", 2));
    mode = 2; calls = 0;
    CHECK(silt_safe_read(0, buffer, sizeof(buffer)) == -1 && errno == EIO && calls == 1);
    mode = 3; calls = 0;
    CHECK(silt_safe_read(0, buffer, sizeof(buffer)) == 0 && calls == 1);
    return 0;
}
