#pragma once

#include <stddef.h>
#include <stdint.h>

// The layout is the Silt libc contract consumed by directory enumeration.
// DIR remains opaque so its remote enumeration cursor can evolve without an
// application ABI change.
typedef struct SiltDirectory DIR;

struct dirent {
    uint64_t d_ino;
    uint8_t d_type;
    char d_name[256];
};

#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
#define DT_LNK 10

DIR* opendir(const char* path);
struct dirent* readdir(DIR* directory);
int closedir(DIR* directory);
void rewinddir(DIR* directory);
