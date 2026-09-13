#pragma once

#include <sys/types.h>

#define WNOHANG 1
#define WUNTRACED 2
#define WCONTINUED 4

#define WIFCONTINUED(status) ((status) == 0xffff)
#define WIFEXITED(status) (((status) & 0xff) == 0)
#define WIFSIGNALED(status) \
    (((status) & 0x7f) > 0 && ((status) & 0x7f) < 0x7f)
#define WIFSTOPPED(status) (((status) & 0xff) == 0x7f)
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WTERMSIG(status) ((status) & 0x7f)
#define WSTOPSIG(status) WEXITSTATUS(status)

pid_t wait(int* status);
pid_t waitpid(pid_t process, int* status, int options);

struct rusage;
pid_t wait3(int* status, int options, struct rusage* usage);
