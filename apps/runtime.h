#pragma once

#include "libneva.h"

static inline uint32_t silt_startup_handle(const char* name, uint32_t type) {
    NevaStartupHandleV1 record;
    return neva_startup_find(name, &record) == NEVA_STATUS_OK
            && (type == 0 || record.object_type == type)
        ? record.handle : NEVA_INVALID_HANDLE;
}

static inline int silt_username(uint16_t uid, char* output,
                                uint32_t capacity) {
    // R0 has two immutable bootstrap identities. NSS/passwd lookup belongs to
    // the later Silt libc layer; keeping this table here avoids ambient VFS.
    const char* name = uid == 0 ? "root" : uid == 1000 ? "session" : NULL;
    if (!name || capacity <= strlen(name)) return -1;
    memcpy(output, name, strlen(name) + 1U);
    return 0;
}
