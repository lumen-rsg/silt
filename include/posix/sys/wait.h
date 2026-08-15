#pragma once

#include_next <sys/wait.h>

struct rusage;
pid_t wait3(int* status, int options, struct rusage* usage);
