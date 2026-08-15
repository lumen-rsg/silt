#pragma once

#include <stddef.h>
#include <stdint.h>

#define SILT_EXEC_INFO_MAGIC 0x31495853U // "SXI1"
#define SILT_EXEC_INFO_VERSION 1U
#define SILT_EXEC_ENVIRONMENT_MAX 64U
#define SILT_EXEC_DESCRIPTOR_MAX 32U
#define SILT_EXEC_DESCRIPTION_MAX 32U
#define SILT_EXEC_PIPE_MAX 8U
#define SILT_EXEC_STRING_BYTES 2048U

#define SILT_EXEC_PIPE_CLOSE_READ  (1U << 0)
#define SILT_EXEC_PIPE_CLOSE_WRITE (1U << 1)

typedef struct {
    uint16_t offset;
    uint16_t length;
} SiltExecStringV1;

typedef struct {
    uint8_t descriptor;
    uint8_t description;
    uint8_t flags;
    uint8_t reserved;
} SiltExecDescriptorV1;

typedef struct {
    uint8_t identifier;
    uint8_t kind;
    uint8_t pipe_identifier;
    uint8_t reserved0;
    uint32_t handle;
    uint32_t rights;
    int32_t status_flags;
    uint64_t offset;
    uint16_t path_offset;
    uint16_t path_length;
    uint32_t reserved1;
} SiltExecDescriptionV1;

typedef struct {
    uint8_t identifier;
    uint8_t flags;
    uint16_t reserved;
    uint32_t shm;
    uint32_t readable;
    uint32_t writable;
} SiltExecPipeV1;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t total_size;
    uint16_t environment_count;
    uint16_t descriptor_count;
    uint16_t description_count;
    uint16_t pipe_count;
    uint16_t string_bytes;
    uint16_t reserved;
    SiltExecStringV1 environment[SILT_EXEC_ENVIRONMENT_MAX];
    SiltExecDescriptorV1 descriptors[SILT_EXEC_DESCRIPTOR_MAX];
    SiltExecDescriptionV1 descriptions[SILT_EXEC_DESCRIPTION_MAX];
    SiltExecPipeV1 pipes[SILT_EXEC_PIPE_MAX];
    char strings[SILT_EXEC_STRING_BYTES];
} SiltExecInfoV1;

_Static_assert(sizeof(SiltExecInfoV1) <= 4096U,
               "Silt exec handoff must fit one Neva page");

static inline int silt_exec_string_append(SiltExecInfoV1* info,
                                          const char* value, size_t length,
                                          uint16_t* offset_out) {
    if (!info || !value || !offset_out || length > UINT16_MAX
        || info->string_bytes > SILT_EXEC_STRING_BYTES
        || length + 1U > SILT_EXEC_STRING_BYTES - info->string_bytes) {
        return -1;
    }
    uint16_t offset = info->string_bytes;
    for (size_t index = 0; index < length; index++) {
        info->strings[offset + index] = value[index];
    }
    info->strings[offset + length] = '\0';
    info->string_bytes += (uint16_t)(length + 1U);
    *offset_out = offset;
    return 0;
}
