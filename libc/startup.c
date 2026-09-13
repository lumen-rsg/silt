#include "silt_internal.h"

#include <stdint.h>
#include <unistd.h>

extern int main(int argc, char* argv[]);

__attribute__((noreturn)) void silt_start(int argc, char* argv[],
                                          uintptr_t exec_info_address) {
    const SiltExecInfoV3* info = (const SiltExecInfoV3*)exec_info_address;
    if (info && (info->magic != SILT_EXEC_INFO_MAGIC
        || info->version != SILT_EXEC_INFO_VERSION
        || info->total_size != sizeof(*info))) _exit(127);
    if (info && info->magic == SILT_EXEC_INFO_MAGIC
        && info->version == SILT_EXEC_INFO_VERSION
        && info->total_size == sizeof(*info)) {
        silt_environment_exec_restore(info);
        if (silt_descriptors_exec_restore(info) < 0) _exit(127);
    }
    _exit(main(argc, argv));
}
