#pragma once
#include <errno.h>
#include <string.h>
#include "filesystem_service.h"

static int silt_normalize_path(const char* cwd, const char* path,
                               char output[NEVA_FS_PATH_MAX + 1U]) {
    if (!path || !path[0]) {
        errno = ENOENT;
        return -1;
    }
    char combined[NEVA_FS_PATH_MAX + 1U];
    size_t length = strlen(path);
    if (path[0] == '/') {
        if (length > NEVA_FS_PATH_MAX) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(combined, path, length + 1U);
    } else {
        size_t cwd_length = strlen(cwd);
        size_t separator = cwd_length > 1U ? 1U : 0U;
        if (cwd_length + separator + length > NEVA_FS_PATH_MAX) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(combined, cwd, cwd_length);
        if (separator) combined[cwd_length++] = '/';
        memcpy(combined + cwd_length, path, length + 1U);
    }

    size_t written = 1U;
    output[0] = '/';
    output[1] = '\0';
    const char* cursor = combined;
    while (*cursor) {
        while (*cursor == '/') cursor++;
        if (!*cursor) break;
        const char* component = cursor;
        while (*cursor && *cursor != '/') cursor++;
        size_t component_length = (size_t)(cursor - component);
        if (component_length == 1U && component[0] == '.') continue;
        if (component_length == 2U && component[0] == '.'
            && component[1] == '.') {
            if (written > 1U) {
                written--;
                while (written > 1U && output[written - 1U] != '/') written--;
                if (written > 1U) written--;
                output[written] = '\0';
            }
            continue;
        }
        if (component_length > NEVA_FS_COMPONENT_MAX
            || written + (written > 1U ? 1U : 0U) + component_length
                   > NEVA_FS_PATH_MAX) {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (written > 1U) output[written++] = '/';
        memcpy(output + written, component, component_length);
        written += component_length;
        output[written] = '\0';
    }
    return 0;
}
