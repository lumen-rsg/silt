#include "runtime.h"

void main(void) {
    uint32_t system = silt_startup_handle("system", NEVA_OBJECT_SYSTEM);
    uint64_t info[4] = { 0 };
    if (!system || sys_rpc(
            system, SYSTEM_RPC_MEM_INFO,
            (uint64_t)(uintptr_t)info, sizeof(info)) != 4) {
        neva_println("meminfo: system capability unavailable");
        sys_exit(1);
    }
    neva_print("Kernel heap: ");
    neva_print_int(info[0]);
    neva_print(" / ");
    neva_print_int(info[1]);
    neva_println(" bytes");
    neva_print("Physical pages: ");
    neva_print_int(info[2]);
    neva_print(" free / ");
    neva_print_int(info[3]);
    neva_println(" total");
    sys_exit(0);
}
