#pragma once

// Service object identities are 64-bit. The bare-metal newlib defaults to
// 16-bit inode/device types, which would alias unrelated provider objects.
#define __machine_ino_t_defined 1
typedef unsigned long __ino_t;
#define __machine_dev_t_defined 1
typedef unsigned long __dev_t;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include_next <sys/_types.h>
#pragma GCC diagnostic pop
