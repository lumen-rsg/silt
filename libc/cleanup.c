#include "libneva.h"
#include "cleanup.h"
#include <setjmp.h>

// The last word is Silt's cleanup checkpoint, not an application signal mask.
_Static_assert(sizeof(jmp_buf) == 176, "Silt A64 setjmp layout changed");

SiltCleanup* g_silt_cleanup_head;

uint32_t silt_cleanup_begin(SiltCleanup* cleanup) {
    uint32_t mask = (uint32_t)sys_signal_mask(0, UINT32_MAX);
    *cleanup = (SiltCleanup){ .previous = g_silt_cleanup_head };
    g_silt_cleanup_head = cleanup;
    return mask;
}

uint32_t silt_cleanup_adopt(SiltCleanup* cleanup, uint32_t handle) {
    if (g_silt_cleanup_head != cleanup || cleanup->handle) sys_exit(127);
    // Temporary ownership must not escape if a handler successfully execs.
    if (handle && sys_handle_set_flags(handle, HANDLE_FLAG_CLOEXEC) != NEVA_STATUS_OK) {
        (void)sys_handle_close(handle);
        handle = 0;
    }
    cleanup->handle = handle;
    return handle;
}

void silt_cleanup_ready(uint32_t mask) {
    (void)sys_signal_mask(2, mask);
}

static void cleanup_pop(void) {
    SiltCleanup* cleanup = g_silt_cleanup_head;
    g_silt_cleanup_head = cleanup->previous;
    uint32_t handle = cleanup->handle;
    cleanup->handle = 0;
    if (handle) (void)sys_handle_close(handle);
}

void silt_cleanup_end(SiltCleanup* cleanup) {
    uint32_t mask = (uint32_t)sys_signal_mask(0, UINT32_MAX);
    // A corrupt chain must never close arbitrary or application-owned handles.
    if (g_silt_cleanup_head != cleanup) sys_exit(127);
    cleanup_pop();
    silt_cleanup_ready(mask);
}

void silt_cleanup_unwind(SiltCleanup* checkpoint) {
    uint32_t mask = (uint32_t)sys_signal_mask(0, UINT32_MAX);
    SiltCleanup* cursor = g_silt_cleanup_head;
    while (cursor && cursor != checkpoint) cursor = cursor->previous;
    if (cursor != checkpoint) sys_exit(127);
    while (g_silt_cleanup_head != checkpoint) cleanup_pop();
    silt_cleanup_ready(mask);
}
