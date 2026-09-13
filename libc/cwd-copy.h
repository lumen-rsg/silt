#pragma once
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static char* silt_copy_cwd(const char* cwd, char* buffer, size_t size) {
    size_t required = strlen(cwd) + 1U;
    // Reject an undersized request before allocating an unreachable buffer.
    if ((buffer || size) && size < required) {
        errno = ERANGE;
        return NULL;
    }
    if (!buffer) {
        buffer = malloc(size ? size : required);
        if (!buffer) {
            errno = ENOMEM;
            return NULL;
        }
    }
    memcpy(buffer, cwd, required);
    return buffer;
}
