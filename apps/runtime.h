#pragma once

#include "libneva.h"

#include <unistd.h>

static inline void silt_app_putc(char value) {
    (void)write(STDOUT_FILENO, &value, 1);
}

static inline void silt_app_print(const char* value) {
    if (value) (void)write(STDOUT_FILENO, value, strlen(value));
}

static inline void silt_app_println(const char* value) {
    silt_app_print(value);
    silt_app_putc('\n');
}

static inline void silt_app_print_int(uint64_t value) {
    char digits[21];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value && count < sizeof(digits));
    while (count > 0) silt_app_putc(digits[--count]);
}

#define neva_putc silt_app_putc
#define neva_print silt_app_print
#define neva_println silt_app_println
#define neva_print_int silt_app_print_int

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
