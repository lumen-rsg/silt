#include "runtime.h"

static volatile uint64_t g_silt_true_fp_state;

// Neva's pager acceptance invokes this fixed position-independent leaf from a
// freshly mapped executable VMO. It is part of the current NevFS ABI fixture.
__asm__(
    ".pushsection .text.pager_exec_probe, \"ax\"\n"
    ".balign 16\n"
    ".global pager_exec_probe\n"
    ".type pager_exec_probe, %function\n"
    "pager_exec_probe:\n"
    ".inst 0xd298dc20\n"
    ".inst 0xf2ab47a0\n"
    ".inst 0xd65f03c0\n"
    ".size pager_exec_probe, . - pager_exec_probe\n"
    ".popsection\n"
);

void main(void) {
    uint64_t low;
    uint64_t high;
    uint64_t fpcr;
    uint64_t fpsr;
    __asm__ volatile(
        "umov %0, v31.d[0]\n"
        "umov %1, v31.d[1]\n"
        "mrs %2, fpcr\n"
        "mrs %3, fpsr\n"
        : "=r"(low), "=r"(high), "=r"(fpcr), "=r"(fpsr)
    );
    g_silt_true_fp_state = low | high | fpcr | fpsr;
    sys_exit(g_silt_true_fp_state == 0 ? 0 : 125);
}
