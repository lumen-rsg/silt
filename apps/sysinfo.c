#include "runtime.h"

void main(void) {
    uint32_t system = silt_startup_handle("system", NEVA_OBJECT_SYSTEM);
    uint64_t memory[4] = { 0 };
    if (!system || sys_rpc(
            system, SYSTEM_RPC_MEM_INFO,
            (uint64_t)(uintptr_t)memory, sizeof(memory)) != 4) {
        neva_println("sysinfo: system capability unavailable");
        sys_exit(1);
    }
    neva_println("OS: Silt 0.1.0");
    neva_println("Kernel: Neva 0.1.0");
    neva_println("Architecture: aarch64");
    neva_print("CPU: ");
    neva_print_int(sys_rpc(system, SYSTEM_RPC_GET_CPU_ID, 0, 0));
    neva_println(" (current)");
    neva_print("Uptime: ");
    neva_print_int(sys_rpc(system, SYSTEM_RPC_UPTIME, 0, 0));
    neva_println(" ms");
    neva_print("Memory: ");
    neva_print_int(memory[3] - memory[2]);
    neva_print(" / ");
    neva_print_int(memory[3]);
    neva_println(" pages");
    neva_print("Identity: uid=");
    neva_print_int(neva_geteuid());
    neva_println("");
    sys_exit(0);
}
