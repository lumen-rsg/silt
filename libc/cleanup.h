#pragma once

#include <stdint.h>

// One EL0 thread per Silt process. Guards are strictly nested stack objects.
typedef struct SiltCleanup {
    struct SiltCleanup* previous;
    uint32_t handle;
} SiltCleanup;

extern SiltCleanup* g_silt_cleanup_head;

// begin blocks maskable signals until the acquired handle has been adopted.
// Adopt handle, then restore the returned mask with silt_cleanup_ready.
uint32_t silt_cleanup_begin(SiltCleanup* cleanup);
uint32_t silt_cleanup_adopt(SiltCleanup* cleanup, uint32_t handle);
void silt_cleanup_ready(uint32_t mask);
void silt_cleanup_end(SiltCleanup* cleanup);
void silt_cleanup_unwind(SiltCleanup* checkpoint);
