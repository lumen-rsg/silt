#include "runtime.h"

typedef struct {
    uint32_t pid;
    uint32_t state;
    uint32_t parent_pid;
    uint32_t uid;
    uint32_t gid;
    uint32_t reserved;
} SiltProcessInfo;

void main(void) {
    uint32_t system = silt_startup_handle("system", NEVA_OBJECT_SYSTEM);
    SiltProcessInfo entries[32];
    int count = system ? (int)sys_rpc(
        system, SYSTEM_RPC_PROC_LIST,
        (uint64_t)(uintptr_t)entries, sizeof(entries)) : -1;
    if (count < 0 || count > 32) {
        neva_println("ps: process inspection unavailable");
        sys_exit(1);
    }
    static const char* states[] = {
        "ready", "running", "blocked", "dying", "zombie", "reapable",
    };
    neva_println("PID  PPID UID  STATE");
    for (int index = 0; index < count; index++) {
        neva_print_int(entries[index].pid);
        neva_print("    ");
        neva_print_int(entries[index].parent_pid);
        neva_print("    ");
        neva_print_int(entries[index].uid);
        neva_print("    ");
        neva_println(entries[index].state < 6
            ? states[entries[index].state] : "unknown");
    }
    sys_exit(0);
}
