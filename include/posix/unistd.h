#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include_next <unistd.h>
#pragma GCC diagnostic pop

pid_t getsid(pid_t process);
pid_t getpgid(pid_t process);
