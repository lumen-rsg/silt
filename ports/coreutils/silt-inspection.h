#pragma once
#include "silt-streams.h"
#include <dirent.h>
#include "exitfail.h"
#undef SAME_INODE
#include "same-inode.h"

extern char** environ;
static inline void initialize_exit_failure(int status) { exit_failure = status; }
static inline char* bad_cast(const char* text) { return (char*)text; }

#define D_INO(entry) ((entry)->d_ino)
#define NOT_AN_INODE_NUMBER 0
#define _D_EXACT_NAMLEN(entry) strlen((entry)->d_name)

// Silt directory streams retain a capability, not a POSIX file descriptor.
// GNU pwd explicitly supports an unavailable dirfd and then uses chdir/stat.
static inline int silt_pwd_dirfd(DIR* directory) {
    (void)directory;
    errno = ENOTSUP;
    return -1;
}
#define dirfd silt_pwd_dirfd

static inline struct dirent* readdir_ignoring_dot_and_dotdot(DIR* directory) {
    struct dirent* entry;
    do { entry = readdir(directory); }
    while (entry && (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")));
    return entry;
}
