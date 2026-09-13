#include "path-normalize.h"
#include "cwd-copy.h"
#include "libneva.h"
#include "filesystem_service.h"
#include "byte_stream.h"
#include "silt_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include <unistd.h>

_Static_assert(PIPE_BUF == NEVA_BYTE_STREAM_IO_MAX, "Silt pipe atomic-write contract drift");

#define SILT_DESCRIPTOR_MAX 32
#define SILT_DESCRIPTION_MAX 32
#define SILT_IO_BYTES 4096U

typedef enum {
    SILT_DESCRIPTION_FREE = 0,
    SILT_DESCRIPTION_TTY,
    SILT_DESCRIPTION_NULL,
    SILT_DESCRIPTION_FILE,
    SILT_DESCRIPTION_DIRECTORY,
    SILT_DESCRIPTION_PIPE_READ,
    SILT_DESCRIPTION_PIPE_WRITE,
    SILT_DESCRIPTION_PTY_SLAVE,
    SILT_DESCRIPTION_PTY_MASTER,
} SiltDescriptionKind;

typedef struct {
    SiltDescriptionKind kind;
    uint32_t handle;
    uint32_t rights;
    uint32_t references;
    int status_flags;
    uint64_t offset;
    char path[NEVA_FS_PATH_MAX + 1U];
} SiltOpenDescription;

typedef struct {
    uint8_t description;
    uint8_t flags;
} SiltDescriptor;

struct SiltDirectory {
    uint32_t handle;
    uint32_t shm;
    uint8_t* mapping;
    uint16_t count;
    uint16_t index;
    struct dirent current;
};

static SiltDescriptor g_descriptors[SILT_DESCRIPTOR_MAX];
static SiltOpenDescription g_descriptions[SILT_DESCRIPTION_MAX];
static int g_initialized;
static char g_cwd[NEVA_FS_PATH_MAX + 1U] = "/";

static int status_errno(NevaStatus status) {
    switch (status) {
        case NEVA_STATUS_INVALID_ARGUMENT: return EINVAL;
        case NEVA_STATUS_NO_MEMORY:
        case NEVA_STATUS_QUOTA_EXCEEDED:
        case NEVA_STATUS_LIMIT_REACHED: return ENOMEM;
        case NEVA_STATUS_BAD_HANDLE: return EBADF;
        case NEVA_STATUS_ACCESS_DENIED: return EACCES;
        case NEVA_STATUS_ALREADY_EXISTS: return EEXIST;
        case NEVA_STATUS_NOT_FOUND:
        case NEVA_STATUS_STALE: return ENOENT;
        case NEVA_STATUS_NOT_SUPPORTED: return ENOSYS;
        case NEVA_STATUS_OVERFLOW: return EOVERFLOW;
        case NEVA_STATUS_FAULT: return EFAULT;
        case NEVA_STATUS_IO:
        case NEVA_STATUS_PEER_CLOSED:
        case NEVA_STATUS_PEER_RESTARTED:
        case NEVA_STATUS_BAD_STATE: return EIO;
        default: return EIO;
    }
}

static uint32_t root_handle(void) {
    NevaStartupHandleV1 record;
    return neva_startup_find("fs.root", &record) == NEVA_STATUS_OK
        && record.object_type == NEVA_OBJECT_TYPE_NAMESPACE_VIEW
        ? record.handle : NEVA_INVALID_HANDLE;
}

static int normalize_path(const char* path, char output[NEVA_FS_PATH_MAX + 1U]) {
    return silt_normalize_path(g_cwd, path, output);
}

static NevaServiceResult resolve_path(const char* path, uint32_t flags,
                                      uint32_t rights) {
    uint32_t root = root_handle();
    size_t length = strlen(path);
    if (!root || length == 0 || length > NEVA_FS_PATH_MAX) {
        return (NevaServiceResult){ .status = NEVA_STATUS_NOT_FOUND };
    }
    uint8_t payload[sizeof(NevaFsResolveV1) + NEVA_FS_PATH_MAX] = { 0 };
    NevaFsResolveV1* request = (NevaFsResolveV1*)payload;
    *request = (NevaFsResolveV1){
        .magic = NEVA_RESOLVE_MAGIC,
        .version = NEVA_FILESYSTEM_ABI_VERSION,
        .size = sizeof(*request),
        .path_offset = sizeof(*request),
        .path_length = (uint16_t)length,
        .resolve_flags = flags,
        .requested_rights = rights,
        .expected_namespace_generation = 1,
    };
    memcpy(payload + sizeof(*request), path, length);
    return sys_service_call(
        root, NAMESPACE_RPC_RESOLVE, (uint64_t)(uintptr_t)payload,
        0, 0, sizeof(*request) + (uint32_t)length, NEVA_DEADLINE_INFINITE);
}

static int executable_header(uint32_t file, char header[SILT_SHEBANG_BYTES]) {
    NevaRemoteFileInfoV1 info = { 0 };
    NevaServiceResult query = sys_service_call(file, REMOTE_FILE_RPC_QUERY,
        (uintptr_t)&info, 0, 0, sizeof(info), NEVA_DEADLINE_INFINITE);
    if (query.status != NEVA_STATUS_OK) { errno = status_errno(query.status); return -1; }
    if (info.magic != NEVA_FILE_INFO_MAGIC || info.version != NEVA_FILESYSTEM_ABI_VERSION
        || info.size != sizeof(info)) { errno = ENOEXEC; return -1; }
    // Resolution may attenuate unsupported rights rather than reject the whole
    // request. Reading a script alone does not authorize executing it.
    if (!(info.rights & HANDLE_RIGHT_EXECUTE_MAP) || !(info.mode & 0111U)) {
        errno = EACCES;
        return -1;
    }
    uint32_t count = info.byte_size < SILT_SHEBANG_BYTES ? (uint32_t)info.byte_size : SILT_SHEBANG_BYTES;
    memset(header, 0, SILT_SHEBANG_BYTES);
    if (!count) { errno = ENOEXEC; return -1; }
    uint32_t shm = sys_shm_create(SILT_IO_BYTES);
    void* mapping = shm ? sys_shm_map(shm) : (void*)-1;
    uint32_t transfer = mapping != (void*)-1 ? sys_handle_dup(shm,
        HANDLE_RIGHT_READ | HANDLE_RIGHT_WRITE | HANDLE_RIGHT_TRANSFER) : 0;
    NevaFileIoV1 request = {
        .magic = NEVA_FILE_IO_MAGIC, .version = NEVA_FILESYSTEM_ABI_VERSION,
        .size = sizeof(request), .data_region_handle = transfer, .data_length = count,
        .expected_change_generation = info.change_generation,
        .absolute_deadline_ns = NEVA_DEADLINE_INFINITE,
    };
    NevaServiceResult result = transfer ? sys_service_call(file, REMOTE_FILE_RPC_READ_AT,
        (uintptr_t)&request, 0, transfer, sizeof(request), NEVA_DEADLINE_INFINITE)
        : (NevaServiceResult){ .status = NEVA_STATUS_NO_MEMORY };
    if (result.status == NEVA_STATUS_OK) memcpy(header, mapping, count);
    if (transfer) (void)sys_handle_close(transfer);
    if (mapping != (void*)-1) (void)sys_shm_unmap(shm);
    else if (shm) (void)sys_handle_close(shm);
    if (result.status != NEVA_STATUS_OK) { errno = status_errno(result.status); return -1; }
    return 0;
}

uint32_t silt_resolve_executable(const char* path, char header[SILT_SHEBANG_BYTES]) {
    char normalized[NEVA_FS_PATH_MAX + 1U];
    if (normalize_path(path, normalized) < 0) return NEVA_INVALID_HANDLE;
    NevaServiceResult file = resolve_path(
        normalized, 0,
        HANDLE_RIGHT_READ_DATA | HANDLE_RIGHT_EXECUTE_MAP
            | HANDLE_RIGHT_METADATA_READ);
    if (file.status != NEVA_STATUS_OK || !file.handle) {
        errno = status_errno(file.status);
        return NEVA_INVALID_HANDLE;
    }
    // Read through the same execute-authorized file before allocating a pager
    // binding. Scripts must not consume the provider's finite ELF cache.
    if (executable_header(file.handle, header) < 0) {
        (void)sys_handle_close(file.handle);
        return NEVA_INVALID_HANDLE;
    }
    if (memcmp(header, "\177ELF", 4) != 0) {
        (void)sys_handle_close(file.handle);
        errno = ENOEXEC;
        return NEVA_INVALID_HANDLE;
    }
    NevaServiceResult pager = sys_service_call(
        file.handle, REMOTE_FILE_RPC_OPEN_PAGER, 0, 0, 0, 0,
        NEVA_DEADLINE_INFINITE);
    (void)sys_handle_close(file.handle);
    if (pager.status != NEVA_STATUS_OK || !pager.handle) {
        errno = status_errno(pager.status);
        return NEVA_INVALID_HANDLE;
    }
    NevaServiceResult executable = sys_service_call(
        pager.handle, PAGER_FILE_RPC_CREATE_VM_OBJECT,
        0, 0, 0, 0, NEVA_DEADLINE_INFINITE);
    (void)sys_handle_close(pager.handle);
    if (executable.status != NEVA_STATUS_OK || !executable.handle) {
        errno = status_errno(executable.status);
        return NEVA_INVALID_HANDLE;
    }
    if (neva_prepare_exec_vmo(executable.handle) < 0
        || sys_handle_set_flags(executable.handle, HANDLE_FLAG_CLOEXEC)
               != NEVA_STATUS_OK) {
        (void)sys_handle_close(executable.handle);
        errno = ENOEXEC;
        return NEVA_INVALID_HANDLE;
    }
    return executable.handle;
}

static int description_allocate(SiltDescriptionKind kind, uint32_t handle,
                                uint32_t rights, int flags, const char* path) {
    for (int index = 0; index < SILT_DESCRIPTION_MAX; index++) {
        if (g_descriptions[index].kind != SILT_DESCRIPTION_FREE) continue;
        g_descriptions[index] = (SiltOpenDescription){
            .kind = kind,
            .handle = handle,
            .rights = rights,
            .references = 1,
            .status_flags = flags,
        };
        if (path) strcpy(g_descriptions[index].path, path);
        return index;
    }
    errno = ENFILE;
    return -1;
}

static int descriptor_allocate_from(int description, int minimum, int flags) {
    if (minimum < 0) minimum = 0;
    for (int descriptor = minimum; descriptor < SILT_DESCRIPTOR_MAX; descriptor++) {
        if (g_descriptors[descriptor].description != 0) continue;
        g_descriptors[descriptor] = (SiltDescriptor){
            .description = (uint8_t)(description + 1),
            .flags = (uint8_t)flags,
        };
        return descriptor;
    }
    errno = EMFILE;
    return -1;
}

static int descriptor_capacity(int required) {
    for (int descriptor = 0; descriptor < SILT_DESCRIPTOR_MAX; descriptor++) {
        if (!g_descriptors[descriptor].description && --required == 0) return 0;
    }
    errno = EMFILE;
    return -1;
}

static void descriptors_initialize(void) {
    if (g_initialized) return;
    g_initialized = 1;
    uint32_t tty = neva_tty_handle();
    if (!tty) return;
    int input = description_allocate(
        SILT_DESCRIPTION_TTY, tty, HANDLE_RIGHT_WRITE | HANDLE_RIGHT_RPC,
        O_RDONLY, NULL);
    int output = description_allocate(
        SILT_DESCRIPTION_TTY, tty, HANDLE_RIGHT_WRITE | HANDLE_RIGHT_RPC,
        O_WRONLY, NULL);
    int error = description_allocate(
        SILT_DESCRIPTION_TTY, tty, HANDLE_RIGHT_WRITE | HANDLE_RIGHT_RPC,
        O_WRONLY, NULL);
    if (input >= 0) (void)descriptor_allocate_from(input, STDIN_FILENO, 0);
    if (output >= 0) (void)descriptor_allocate_from(output, STDOUT_FILENO, 0);
    if (error >= 0) (void)descriptor_allocate_from(error, STDERR_FILENO, 0);
}

static SiltOpenDescription* descriptor_get(int descriptor) {
    descriptors_initialize();
    if (descriptor < 0 || descriptor >= SILT_DESCRIPTOR_MAX
        || g_descriptors[descriptor].description == 0) {
        errno = EBADF;
        return NULL;
    }
    return &g_descriptions[g_descriptors[descriptor].description - 1U];
}

static void description_sync_cloexec(int index) {
    SiltOpenDescription* description = &g_descriptions[index];
    if ((description->kind != SILT_DESCRIPTION_FILE
         && description->kind != SILT_DESCRIPTION_DIRECTORY
         && description->kind != SILT_DESCRIPTION_PIPE_READ
         && description->kind != SILT_DESCRIPTION_PIPE_WRITE
         && description->kind != SILT_DESCRIPTION_PTY_SLAVE
         && description->kind != SILT_DESCRIPTION_PTY_MASTER)
        || !description->handle) return;
    int all_cloexec = 1;
    for (int descriptor = 0; descriptor < SILT_DESCRIPTOR_MAX; descriptor++) {
        if (g_descriptors[descriptor].description != (uint8_t)(index + 1)) continue;
        if ((g_descriptors[descriptor].flags & FD_CLOEXEC) == 0) {
            all_cloexec = 0;
            break;
        }
    }
    (void)sys_handle_set_flags(
        description->handle, all_cloexec ? HANDLE_FLAG_CLOEXEC : 0);
}

static void description_release(int index) {
    SiltOpenDescription* description = &g_descriptions[index];
    if (description->references > 1U) {
        description->references--;
        return;
    }
    if ((description->kind == SILT_DESCRIPTION_FILE
         || description->kind == SILT_DESCRIPTION_DIRECTORY
         || description->kind == SILT_DESCRIPTION_PIPE_READ
         || description->kind == SILT_DESCRIPTION_PIPE_WRITE
         || description->kind == SILT_DESCRIPTION_PTY_SLAVE
         || description->kind == SILT_DESCRIPTION_PTY_MASTER)
        && description->handle) {
        (void)sys_handle_close(description->handle);
    }
    memset(description, 0, sizeof(*description));
}

static int descriptor_close(int descriptor) {
    SiltOpenDescription* description = descriptor_get(descriptor);
    if (!description) return -1;
    int index = (int)(description - g_descriptions);
    g_descriptors[descriptor] = (SiltDescriptor){ 0 };
    description_release(index);
    if (g_descriptions[index].kind != SILT_DESCRIPTION_FREE) {
        description_sync_cloexec(index);
    }
    return 0;
}

int pipe(int descriptors[2]) {
    if (!descriptors) {
        errno = EFAULT;
        return -1;
    }
    descriptors_initialize();
    // Refuse local exhaustion before asking the kernel for two endpoints.
    // The caller's array is published only after both descriptors exist.
    if (descriptor_capacity(2) < 0) return -1;
    uint32_t read_handle, write_handle;
    NevaStatus status = sys_byte_stream_create(&read_handle, &write_handle);
    if (status != NEVA_STATUS_OK) {
        errno = status_errno(status);
        return -1;
    }
    int reader = description_allocate(SILT_DESCRIPTION_PIPE_READ,
        read_handle, HANDLE_RIGHT_RPC | HANDLE_RIGHT_READ | HANDLE_RIGHT_TRANSFER,
        O_RDONLY, NULL);
    int writer = description_allocate(SILT_DESCRIPTION_PIPE_WRITE,
        write_handle, HANDLE_RIGHT_RPC | HANDLE_RIGHT_WRITE | HANDLE_RIGHT_TRANSFER,
        O_WRONLY, NULL);
    int read_fd = reader >= 0 ? descriptor_allocate_from(reader, 0, 0) : -1;
    int write_fd = writer >= 0 ? descriptor_allocate_from(writer, 0, 0) : -1;
    if (read_fd < 0 || write_fd < 0) {
        if (read_fd >= 0) g_descriptors[read_fd] = (SiltDescriptor){ 0 };
        if (write_fd >= 0) g_descriptors[write_fd] = (SiltDescriptor){ 0 };
        if (reader >= 0) memset(&g_descriptions[reader], 0, sizeof(g_descriptions[reader]));
        if (writer >= 0) memset(&g_descriptions[writer], 0, sizeof(g_descriptions[writer]));
        (void)sys_handle_close(read_handle);
        (void)sys_handle_close(write_handle);
        errno = EMFILE;
        return -1;
    }
    descriptors[0] = read_fd;
    descriptors[1] = write_fd;
    return 0;
}

static int pipe_io(SiltOpenDescription* description, void* buffer, size_t size,
                   int writing) {
    size_t complete = 0;
    uint64_t epoch = sys_signal_epoch();
    while (complete < size) {
        size_t count = size - complete;
        if (count > NEVA_BYTE_STREAM_IO_MAX) count = NEVA_BYTE_STREAM_IO_MAX;
        uint32_t method = writing ? BYTE_STREAM_RPC_WRITE : BYTE_STREAM_RPC_READ;
        uint64_t address = (uintptr_t)buffer + complete;
        int64_t result = (int64_t)sys_rpc(description->handle, method, address, count);
        if (result == NEVA_STATUS_WOULD_BLOCK) {
            SiltCleanup cleanup;
            uint32_t mask = silt_cleanup_begin(&cleanup);
            int64_t snapshot = (int64_t)sys_rpc(description->handle,
                BYTE_STREAM_RPC_DUP_CHANGE_EVENT, 0, 0);
            if (snapshot > 0 && snapshot <= UINT32_MAX) {
                snapshot = silt_cleanup_adopt(&cleanup, (uint32_t)snapshot);
            }
            silt_cleanup_ready(mask);
            if (snapshot <= 0 || snapshot > UINT32_MAX) {
                silt_cleanup_end(&cleanup);
                errno = snapshot < 0 ? status_errno((NevaStatus)snapshot) : EIO;
                return complete ? (int)complete : -1;
            }
            uint32_t event = (uint32_t)snapshot;
            // Subscribe, then retry: progress or peer death between the first
            // attempt and subscription must not become a lost wakeup.
            result = (int64_t)sys_rpc(description->handle, method, address, count);
            if (result == NEVA_STATUS_WOULD_BLOCK) {
                NevaStatus status = sys_event_wait_epoch(event, NEVA_DEADLINE_INFINITE, epoch);
                silt_cleanup_end(&cleanup);
                if (status == NEVA_STATUS_OK) continue;
                errno = status == NEVA_STATUS_INTERRUPTED || status == NEVA_STATUS_CANCELLED
                    ? EINTR : status_errno(status);
                return complete ? (int)complete : -1;
            }
            silt_cleanup_end(&cleanup);
        }
        if (result < 0) {
            if (writing && result == NEVA_STATUS_PEER_CLOSED) {
                (void)raise(SIGPIPE);
                errno = EPIPE;
            } else {
                errno = status_errno((NevaStatus)result);
            }
            return complete ? (int)complete : -1;
        }
        if (!writing) return (int)result;
        complete += (size_t)result;
    }
    return (int)complete;
}

void silt_descriptors_process_exit(void) {
    if (!g_initialized) return;
    for (int descriptor = 0; descriptor < SILT_DESCRIPTOR_MAX; descriptor++) {
        if (g_descriptors[descriptor].description != 0) {
            (void)descriptor_close(descriptor);
        }
    }
}

static int description_exec_survives(int description) {
    for (int descriptor = 0; descriptor < SILT_DESCRIPTOR_MAX; descriptor++) {
        if (g_descriptors[descriptor].description
                == (uint8_t)(description + 1)
            && (g_descriptors[descriptor].flags & FD_CLOEXEC) == 0) {
            return 1;
        }
    }
    return 0;
}

int silt_descriptors_exec_export(SiltExecInfoV3* info) {
    if (!info) return -1;
    descriptors_initialize();
    info->creation_mask = silt_creation_mask();
    info->working_directory.length = (uint16_t)strlen(g_cwd);
    if (silt_exec_string_append(info, g_cwd, info->working_directory.length,
            &info->working_directory.offset) < 0) return -1;
    for (int description = 0; description < SILT_DESCRIPTION_MAX;
         description++) {
        SiltOpenDescription* source = &g_descriptions[description];
        if (source->references == 0 || !description_exec_survives(description)) {
            continue;
        }
        if (info->description_count == SILT_EXEC_DESCRIPTION_MAX) return -1;
        SiltExecDescriptionV2* target =
            &info->descriptions[info->description_count++];
        *target = (SiltExecDescriptionV2){
            .identifier = (uint8_t)description,
            .kind = (uint8_t)source->kind,
            .handle = source->handle,
            .rights = source->rights,
            .status_flags = source->status_flags,
            .offset = source->offset,
        };
        if (source->path[0]) {
            size_t length = strlen(source->path);
            if (length > UINT16_MAX
                || silt_exec_string_append(
                       info, source->path, length, &target->path_offset) < 0) {
                return -1;
            }
            target->path_length = (uint16_t)length;
        }
    }
    for (int descriptor = 0; descriptor < SILT_DESCRIPTOR_MAX; descriptor++) {
        if (g_descriptors[descriptor].description == 0
            || (g_descriptors[descriptor].flags & FD_CLOEXEC) != 0) {
            continue;
        }
        if (info->descriptor_count == SILT_EXEC_DESCRIPTOR_MAX) return -1;
        info->descriptors[info->descriptor_count++] = (SiltExecDescriptorV2){
            .descriptor = (uint8_t)descriptor,
            .description = (uint8_t)(g_descriptors[descriptor].description - 1U),
            .flags = g_descriptors[descriptor].flags,
        };
    }
    return 0;
}

int silt_descriptors_exec_restore(const SiltExecInfoV3* info) {
    if (!info || info->descriptor_count > SILT_EXEC_DESCRIPTOR_MAX
        || info->description_count > SILT_EXEC_DESCRIPTION_MAX
        || info->string_bytes > SILT_EXEC_STRING_BYTES) {
        return -1;
    }
    uint16_t cwd_offset = info->working_directory.offset;
    uint16_t cwd_length = info->working_directory.length;
    if (!cwd_length || cwd_length > NEVA_FS_PATH_MAX || cwd_offset >= info->string_bytes
        || cwd_length >= info->string_bytes - cwd_offset
        || info->strings[cwd_offset] != '/' || info->strings[cwd_offset + cwd_length]
        || strlen(info->strings + cwd_offset) != cwd_length
        || (info->creation_mask & ~0777U)) return -1;
    char normalized[NEVA_FS_PATH_MAX + 1U];
    if (normalize_path(info->strings + cwd_offset, normalized) < 0
        || strcmp(normalized, info->strings + cwd_offset) != 0) return -1;
    strcpy(g_cwd, normalized);
    (void)umask(info->creation_mask);
    memset(g_descriptors, 0, sizeof(g_descriptors));
    memset(g_descriptions, 0, sizeof(g_descriptions));
    for (uint16_t index = 0; index < info->description_count; index++) {
        const SiltExecDescriptionV2* source = &info->descriptions[index];
        if (source->identifier >= SILT_DESCRIPTION_MAX
            || source->kind == SILT_DESCRIPTION_FREE
            || source->kind > SILT_DESCRIPTION_PTY_MASTER
            || g_descriptions[source->identifier].kind
                != SILT_DESCRIPTION_FREE) {
            return -1;
        }
        SiltOpenDescription* target = &g_descriptions[source->identifier];
        *target = (SiltOpenDescription){
            .kind = (SiltDescriptionKind)source->kind,
            .handle = source->handle,
            .rights = source->rights,
            .status_flags = source->status_flags,
            .offset = source->offset,
        };
        if (source->path_length) {
            if (source->path_length > NEVA_FS_PATH_MAX
                || source->path_offset > info->string_bytes
                || source->path_length >= info->string_bytes
                        - source->path_offset
                || info->strings[source->path_offset + source->path_length]
                    != '\0') {
                return -1;
            }
            memcpy(target->path, &info->strings[source->path_offset],
                   source->path_length + 1U);
        }
    }
    for (uint16_t index = 0; index < info->descriptor_count; index++) {
        const SiltExecDescriptorV2* source = &info->descriptors[index];
        if (source->descriptor >= SILT_DESCRIPTOR_MAX
            || source->description >= SILT_DESCRIPTION_MAX
            || g_descriptors[source->descriptor].description != 0
            || g_descriptions[source->description].kind
                == SILT_DESCRIPTION_FREE) {
            return -1;
        }
        g_descriptors[source->descriptor] = (SiltDescriptor){
            .description = (uint8_t)(source->description + 1U),
            .flags = source->flags,
        };
        g_descriptions[source->description].references++;
    }
    for (int description = 0; description < SILT_DESCRIPTION_MAX;
         description++) {
        if (g_descriptions[description].kind != SILT_DESCRIPTION_FREE
            && g_descriptions[description].references == 0) {
            return -1;
        }
    }
    g_initialized = 1;
    return 0;
}

static int query_file(uint32_t handle, NevaRemoteFileInfoV1* info) {
    NevaServiceResult result = sys_service_call(
        handle, REMOTE_FILE_RPC_QUERY, (uint64_t)(uintptr_t)info,
        0, 0, sizeof(*info), NEVA_DEADLINE_INFINITE);
    if (result.status != NEVA_STATUS_OK) {
        errno = status_errno(result.status);
        return -1;
    }
    if (info->magic != NEVA_FILE_INFO_MAGIC
        || info->version != NEVA_FILESYSTEM_ABI_VERSION || info->size != sizeof(*info)) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static int resize_file(uint32_t handle, uint64_t size) {
    NevaRemoteFileInfoV1 info;
    if (query_file(handle, &info) < 0) return -1;
    NevaFileResizeV1 resize = {
        .magic = NEVA_FILE_RESIZE_MAGIC,
        .version = NEVA_FILESYSTEM_ABI_VERSION,
        .size = sizeof(resize),
        .byte_size = size,
        .expected_change_generation = info.change_generation,
    };
    NevaServiceResult result = sys_service_call(
        handle, REMOTE_FILE_RPC_RESIZE, (uint64_t)(uintptr_t)&resize,
        0, 0, sizeof(resize), NEVA_DEADLINE_INFINITE);
    if (result.status != NEVA_STATUS_OK) {
        errno = status_errno(result.status);
        return -1;
    }
    return 0;
}

static NevaServiceResult create_file(const char* path, uint32_t rights,
                                     mode_t mode) {
    char parent[NEVA_FS_PATH_MAX + 1U];
    strcpy(parent, path);
    char* slash = strrchr(parent, '/');
    const char* name = slash ? slash + 1 : parent;
    size_t name_length = strlen(name);
    if (name_length == 0 || name_length > NEVA_FS_COMPONENT_MAX) {
        return (NevaServiceResult){ .status = NEVA_STATUS_INVALID_ARGUMENT };
    }
    char leaf[NEVA_FS_COMPONENT_MAX + 1U];
    strcpy(leaf, name);
    if (slash == parent) parent[1] = '\0';
    else if (slash) *slash = '\0';
    else strcpy(parent, g_cwd);
    if (strcmp(parent, "/") == 0) {
        return (NevaServiceResult){ .status = NEVA_STATUS_ACCESS_DENIED };
    }
    NevaServiceResult directory = resolve_path(
        parent, NEVA_RESOLVE_FLAG_DIRECTORY,
        HANDLE_RIGHT_LOOKUP | HANDLE_RIGHT_CREATE | HANDLE_RIGHT_METADATA_READ);
    if (directory.status != NEVA_STATUS_OK || !directory.handle) return directory;
    NevaDirectoryInfoV1 info;
    NevaServiceResult queried = sys_service_call(
        directory.handle, DIRECTORY_RPC_QUERY, (uint64_t)(uintptr_t)&info,
        0, 0, sizeof(info), NEVA_DEADLINE_INFINITE);
    if (queried.status != NEVA_STATUS_OK) {
        (void)sys_handle_close(directory.handle);
        return queried;
    }
    uint8_t payload[sizeof(NevaFsMutationV1) + NEVA_FS_COMPONENT_MAX] = { 0 };
    NevaFsMutationV1* request = (NevaFsMutationV1*)payload;
    *request = (NevaFsMutationV1){
        .magic = NEVA_MUTATION_MAGIC,
        .version = NEVA_FILESYSTEM_ABI_VERSION,
        .size = sizeof(*request),
        .name_offset = sizeof(*request),
        .name_length = (uint16_t)name_length,
        .kind = NEVA_FS_KIND_FILE,
        .requested_rights = rights,
        .mode = mode & 0777U & ~(uint32_t)silt_creation_mask(),
        .expected_directory_generation = info.directory_generation,
    };
    memcpy(payload + sizeof(*request), leaf, name_length);
    NevaServiceResult created = sys_service_call(
        directory.handle, DIRECTORY_RPC_CREATE, (uint64_t)(uintptr_t)payload,
        0, 0, sizeof(*request) + (uint32_t)name_length,
        NEVA_DEADLINE_INFINITE);
    (void)sys_handle_close(directory.handle);
    return created;
}

static int open_terminal(const char* path, int flags, uint64_t identity, int master) {
    SiltCleanup guard;
    uint32_t mask = silt_cleanup_begin(&guard);
    // Recheck after blocking handlers: terminal acquisition must never commit
    // unless the caller can publish its descriptor without another failure.
    if (descriptor_capacity(1) < 0) {
        silt_cleanup_end(&guard);
        silt_cleanup_ready(mask);
        return -1;
    }
    uint32_t handle = silt_cleanup_adopt(&guard, silt_pty_open(identity, flags, master));
    int descriptor = -1;
    if (handle) {
        int description = description_allocate(master ? SILT_DESCRIPTION_PTY_MASTER : SILT_DESCRIPTION_PTY_SLAVE,
            handle, HANDLE_RIGHT_RPC | HANDLE_RIGHT_INSPECT | HANDLE_RIGHT_WRITE | HANDLE_RIGHT_TRANSFER,
            flags, path);
        if (description >= 0) {
            descriptor = descriptor_allocate_from(description, 0, (flags & O_CLOEXEC) ? FD_CLOEXEC : 0);
            guard.handle = 0; // The description owns the reference from here.
            if (descriptor < 0) description_release(description);
            else {
                (void)sys_rpc(handle, REMOTE_OBJECT_RPC_SET_OPEN_FLAGS,
                    (flags & O_NONBLOCK) ? NEVA_REMOTE_OPEN_NONBLOCK : 0, 0);
                description_sync_cloexec(description);
            }
        }
    }
    silt_cleanup_end(&guard);
    silt_cleanup_ready(mask);
    return descriptor;
}

static int pty_path_identity(const char* path, uint64_t* identity);

static int open_normalized(const char* path, int flags, mode_t mode) {
    int access = flags & O_ACCMODE;
    if (access != O_RDONLY && access != O_WRONLY && access != O_RDWR) {
        errno = EINVAL;
        return -1;
    }
    // A full descriptor table must not create or truncate the caller's file.
    // Each live description owns at least one descriptor, so this also
    // guarantees a free description slot at this admission check.
    if (descriptor_capacity(1) < 0) return -1;
    if (strcmp(path, "/dev/ptmx") == 0) {
        if (access != O_RDWR || (flags & ~(O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK))) {
            errno = EINVAL; return -1;
        }
        return open_terminal(path, flags, 0, 1);
    }
    if (strcmp(path, "/dev/tty") == 0) return open_terminal(path, flags, 0, 0);
    if (strncmp(path, "/dev/pts/", 9) == 0) {
        uint64_t identity;
        if (pty_path_identity(path, &identity)) return -1;
        return open_terminal(path, flags, identity, 0);
    }
    if (strcmp(path, "/dev/null") == 0) {
        SiltDescriptionKind kind = SILT_DESCRIPTION_NULL;
        uint32_t tty = 0;
        int description = description_allocate(
            kind, tty, HANDLE_RIGHT_WRITE | HANDLE_RIGHT_RPC, flags, path);
        if (description < 0) return -1;
        int descriptor = descriptor_allocate_from(description, 0,
            (flags & O_CLOEXEC) ? FD_CLOEXEC : 0);
        if (descriptor < 0) description_release(description);
        else description_sync_cloexec(description);
        return descriptor;
    }

    uint32_t rights = HANDLE_RIGHT_METADATA_READ;
    if (access == O_RDONLY || access == O_RDWR) rights |= HANDLE_RIGHT_READ_DATA;
    if (access == O_WRONLY || access == O_RDWR) rights |= HANDLE_RIGHT_WRITE_DATA;
    uint32_t resolve_flags = (flags & O_DIRECTORY)
        ? NEVA_RESOLVE_FLAG_DIRECTORY : 0;
    NevaServiceResult result = resolve_path(path, resolve_flags, rights);
    int existed = result.status == NEVA_STATUS_OK;
    if (!existed && (flags & O_CREAT)) result = create_file(path, rights, mode);
    if (result.status != NEVA_STATUS_OK || !result.handle) {
        errno = status_errno(result.status);
        return -1;
    }
    if (existed && (flags & O_CREAT) && (flags & O_EXCL)) {
        (void)sys_handle_close(result.handle);
        errno = EEXIST;
        return -1;
    }
    if ((flags & O_TRUNC) && access != O_RDONLY
        && resize_file(result.handle, 0) < 0) {
        (void)sys_handle_close(result.handle);
        return -1;
    }
    SiltDescriptionKind kind = (flags & O_DIRECTORY)
        ? SILT_DESCRIPTION_DIRECTORY : SILT_DESCRIPTION_FILE;
    int description = description_allocate(
        kind, result.handle, rights, flags, path);
    if (description < 0) {
        (void)sys_handle_close(result.handle);
        return -1;
    }
    int descriptor = descriptor_allocate_from(description, 0,
        (flags & O_CLOEXEC) ? FD_CLOEXEC : 0);
    if (descriptor < 0) description_release(description);
    else description_sync_cloexec(description);
    return descriptor;
}

int open(const char* path, int flags, ...) {
    descriptors_initialize();
    mode_t mode = 0666;
    if (flags & O_CREAT) {
        va_list arguments;
        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
    }
    char normalized[NEVA_FS_PATH_MAX + 1U];
    if (normalize_path(path, normalized) < 0) return -1;
    return open_normalized(normalized, flags, mode);
}

int openat(int directory, const char* path, int flags, ...) {
    descriptors_initialize();
    mode_t mode = 0666;
    if (flags & O_CREAT) {
        va_list arguments;
        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
    }
    char source[NEVA_FS_PATH_MAX + 1U];
    if (path && path[0] == '/') {
        if (strlen(path) > NEVA_FS_PATH_MAX) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(source, path);
    } else if (directory == AT_FDCWD) {
        if (!path || strlen(g_cwd) + strlen(path) + 2U > sizeof(source)) {
            errno = path ? ENAMETOOLONG : EFAULT;
            return -1;
        }
        strcpy(source, g_cwd);
        if (strcmp(source, "/") != 0) strcat(source, "/");
        strcat(source, path);
    } else {
        SiltOpenDescription* description = descriptor_get(directory);
        if (!description) return -1;
        if (description->kind != SILT_DESCRIPTION_DIRECTORY) {
            errno = ENOTDIR;
            return -1;
        }
        if (!path || strlen(description->path) + strlen(path) + 2U > sizeof(source)) {
            errno = path ? ENAMETOOLONG : EFAULT;
            return -1;
        }
        strcpy(source, description->path);
        if (strcmp(source, "/") != 0) strcat(source, "/");
        strcat(source, path);
    }
    char normalized[NEVA_FS_PATH_MAX + 1U];
    if (normalize_path(source, normalized) < 0) return -1;
    return open_normalized(normalized, flags, mode);
}

int close(int descriptor) {
    return descriptor_close(descriptor);
}

static int remote_io(SiltOpenDescription* description, void* buffer,
                     size_t size, int write_operation) {
    size_t complete = 0;
    while (complete < size) {
        uint32_t chunk = (uint32_t)(size - complete);
        if (chunk > SILT_IO_BYTES) chunk = SILT_IO_BYTES;
        NevaRemoteFileInfoV1 info;
        if (query_file(description->handle, &info) < 0) {
            return complete ? (int)complete : -1;
        }
        if (!write_operation) {
            if (description->offset >= info.byte_size) break;
            uint64_t remaining = info.byte_size - description->offset;
            if (chunk > remaining) chunk = (uint32_t)remaining;
        } else if (description->status_flags & O_APPEND) {
            description->offset = info.byte_size;
        }
        uint32_t shm = sys_shm_create(SILT_IO_BYTES);
        uint8_t* mapping = shm ? (uint8_t*)sys_shm_map(shm) : (void*)-1;
        uint32_t transfer = mapping != (void*)-1 ? sys_handle_dup(
            shm, HANDLE_RIGHT_READ | HANDLE_RIGHT_WRITE | HANDLE_RIGHT_TRANSFER) : 0;
        if (!shm || mapping == (void*)-1 || !transfer) {
            if (shm && mapping != (void*)-1) (void)sys_shm_unmap(shm);
            else if (shm) (void)sys_handle_close(shm);
            errno = ENOMEM;
            return complete ? (int)complete : -1;
        }
        if (write_operation) memcpy(mapping, (const uint8_t*)buffer + complete, chunk);
        NevaFileIoV1 request = {
            .magic = NEVA_FILE_IO_MAGIC,
            .version = NEVA_FILESYSTEM_ABI_VERSION,
            .size = sizeof(request),
            .offset = description->offset,
            .data_region_handle = transfer,
            .data_length = chunk,
            .expected_change_generation = info.change_generation,
            .absolute_deadline_ns = NEVA_DEADLINE_INFINITE,
        };
        NevaServiceResult result = sys_service_call(
            description->handle,
            write_operation ? REMOTE_FILE_RPC_WRITE_AT : REMOTE_FILE_RPC_READ_AT,
            (uint64_t)(uintptr_t)&request, 0, transfer, sizeof(request),
            NEVA_DEADLINE_INFINITE);
        if (result.status == NEVA_STATUS_OK && !write_operation) {
            memcpy((uint8_t*)buffer + complete, mapping, chunk);
        }
        (void)sys_handle_close(transfer);
        (void)sys_shm_unmap(shm);
        if (result.status != NEVA_STATUS_OK) {
            errno = status_errno(result.status);
            return complete ? (int)complete : -1;
        }
        description->offset += chunk;
        complete += chunk;
    }
    return (int)complete;
}

int read(int descriptor, void* buffer, size_t size) {
    SiltOpenDescription* description = descriptor_get(descriptor);
    if (!description) return -1;
    if (!buffer && size != 0) {
        errno = EFAULT;
        return -1;
    }
    if ((description->status_flags & O_ACCMODE) == O_WRONLY) {
        errno = EBADF;
        return -1;
    }
    if (description->kind == SILT_DESCRIPTION_NULL) return 0;
    if (description->kind == SILT_DESCRIPTION_PIPE_READ) {
        return pipe_io(description, buffer, size, 0);
    }
    if (description->kind == SILT_DESCRIPTION_PIPE_WRITE) {
        errno = EBADF;
        return -1;
    }
    if (description->kind == SILT_DESCRIPTION_TTY || description->kind == SILT_DESCRIPTION_PTY_SLAVE) {
        return silt_tty_read(description->handle, buffer, size, fcntl(descriptor, F_GETFL));
    }
    if (description->kind == SILT_DESCRIPTION_PTY_MASTER) {
        return silt_pty_io(description->handle, buffer, size, fcntl(descriptor, F_GETFL), 0);
    }
    if (description->kind != SILT_DESCRIPTION_FILE) {
        errno = EISDIR;
        return -1;
    }
    return remote_io(description, buffer, size, 0);
}

int write(int descriptor, const void* buffer, size_t size) {
    SiltOpenDescription* description = descriptor_get(descriptor);
    if (!description) return -1;
    if (!buffer && size != 0) {
        errno = EFAULT;
        return -1;
    }
    if ((description->status_flags & O_ACCMODE) == O_RDONLY) {
        errno = EBADF;
        return -1;
    }
    if (description->kind == SILT_DESCRIPTION_NULL) return (int)size;
    if (description->kind == SILT_DESCRIPTION_PIPE_WRITE) {
        return pipe_io(description, (void*)buffer, size, 1);
    }
    if (description->kind == SILT_DESCRIPTION_PIPE_READ) {
        errno = EBADF;
        return -1;
    }
    if (description->kind == SILT_DESCRIPTION_TTY || description->kind == SILT_DESCRIPTION_PTY_SLAVE) {
        return silt_tty_write(description->handle, buffer, size, fcntl(descriptor, F_GETFL));
    }
    if (description->kind == SILT_DESCRIPTION_PTY_MASTER) {
        return silt_pty_io(description->handle, (void*)buffer, size, fcntl(descriptor, F_GETFL), 1);
    }
    if (description->kind != SILT_DESCRIPTION_FILE) {
        errno = EISDIR;
        return -1;
    }
    return remote_io(description, (void*)buffer, size, 1);
}

off_t lseek(int descriptor, off_t offset, int origin) {
    SiltOpenDescription* description = descriptor_get(descriptor);
    if (!description) return (off_t)-1;
    if (description->kind != SILT_DESCRIPTION_FILE) {
        errno = ESPIPE;
        return (off_t)-1;
    }
    uint64_t base = 0;
    if (origin == SEEK_CUR) base = description->offset;
    else if (origin == SEEK_END) {
        NevaRemoteFileInfoV1 info;
        if (query_file(description->handle, &info) < 0) return (off_t)-1;
        base = info.byte_size;
    } else if (origin != SEEK_SET) {
        errno = EINVAL;
        return (off_t)-1;
    }
    if (offset < 0 && (uint64_t)(-offset) > base) {
        errno = EINVAL;
        return (off_t)-1;
    }
    uint64_t next = offset < 0 ? base - (uint64_t)(-offset)
                               : base + (uint64_t)offset;
    if (next > (uint64_t)INT64_MAX) {
        errno = EOVERFLOW;
        return (off_t)-1;
    }
    description->offset = next;
    return (off_t)next;
}

int dup(int descriptor) {
    SiltOpenDescription* description = descriptor_get(descriptor);
    if (!description) return -1;
    int index = (int)(description - g_descriptions);
    int duplicate = descriptor_allocate_from(index, 0, 0);
    if (duplicate >= 0) {
        description->references++;
        description_sync_cloexec(index);
    }
    return duplicate;
}

int dup2(int source, int destination) {
    SiltOpenDescription* description = descriptor_get(source);
    if (!description) return -1;
    if (destination < 0 || destination >= SILT_DESCRIPTOR_MAX) {
        errno = EBADF;
        return -1;
    }
    if (source == destination) return destination;
    if (g_descriptors[destination].description != 0) (void)descriptor_close(destination);
    int index = (int)(description - g_descriptions);
    g_descriptors[destination] = (SiltDescriptor){
        .description = (uint8_t)(index + 1),
    };
    description->references++;
    description_sync_cloexec(index);
    return destination;
}

int fcntl(int descriptor, int command, ...) {
    SiltOpenDescription* description = descriptor_get(descriptor);
    if (!description) return -1;
    va_list arguments;
    va_start(arguments, command);
    int result = -1;
    if (command == F_GETFD) result = g_descriptors[descriptor].flags;
    else if (command == F_SETFD) {
        int flags = va_arg(arguments, int);
        g_descriptors[descriptor].flags = (uint8_t)(flags & FD_CLOEXEC);
        description_sync_cloexec((int)(description - g_descriptions));
        result = 0;
    } else if (command == F_GETFL) {
        result = description->status_flags;
        if (description->kind == SILT_DESCRIPTION_PTY_MASTER || description->kind == SILT_DESCRIPTION_PTY_SLAVE) {
            int64_t flags = (int64_t)sys_rpc(description->handle, REMOTE_OBJECT_RPC_GET_OPEN_FLAGS, 0, 0);
            if (flags < 0) { result = -1; errno = EIO; }
            else result = (result & ~O_NONBLOCK) | ((flags & NEVA_REMOTE_OPEN_NONBLOCK) ? O_NONBLOCK : 0);
        }
    }
    else if (command == F_SETFL) {
        int flags = va_arg(arguments, int);
        if (description->kind == SILT_DESCRIPTION_PTY_MASTER || description->kind == SILT_DESCRIPTION_PTY_SLAVE) {
            int64_t status = (int64_t)sys_rpc(description->handle, REMOTE_OBJECT_RPC_SET_OPEN_FLAGS,
                (flags & O_NONBLOCK) ? NEVA_REMOTE_OPEN_NONBLOCK : 0, 0);
            va_end(arguments);
            if (status < 0) { errno = EIO; return -1; }
            return 0;
        }
        if (flags & O_NONBLOCK) {
            va_end(arguments);
            errno = ENOTSUP;
            return -1;
        }
        description->status_flags = (description->status_flags & ~O_APPEND)
            | (flags & O_APPEND);
        result = 0;
    } else if (command == F_DUPFD || command == F_DUPFD_CLOEXEC) {
        int minimum = va_arg(arguments, int);
        if (minimum < 0 || minimum >= SILT_DESCRIPTOR_MAX) {
            va_end(arguments);
            errno = EINVAL;
            return -1;
        }
        int index = (int)(description - g_descriptions);
        result = descriptor_allocate_from(index, minimum,
            command == F_DUPFD_CLOEXEC ? FD_CLOEXEC : 0);
        if (result >= 0) {
            description->references++;
            description_sync_cloexec(index);
        }
    } else errno = EINVAL;
    va_end(arguments);
    return result;
}

int isatty(int descriptor) {
    SiltOpenDescription* description = descriptor_get(descriptor);
    if (!description) return 0;
    if (description->kind == SILT_DESCRIPTION_TTY || description->kind == SILT_DESCRIPTION_PTY_SLAVE
        || description->kind == SILT_DESCRIPTION_PTY_MASTER) return 1;
    errno = ENOTTY;
    return 0;
}

uint32_t silt_descriptor_tty(int descriptor) {
    SiltOpenDescription* description = descriptor_get(descriptor);
    if (!description) return 0;
    if (description->kind == SILT_DESCRIPTION_TTY || description->kind == SILT_DESCRIPTION_PTY_SLAVE)
        return description->handle;
    errno = ENOTTY;
    return 0;
}

_Static_assert(sizeof(ino_t) == 8 && sizeof(dev_t) == 8, "Silt requires full service identities");

static void fill_file_stat(const NevaRemoteFileInfoV1* info, struct stat* status) {
    memset(status, 0, sizeof(*status));
    status->st_ino = (ino_t)info->object_id;
    status->st_dev = (dev_t)info->volume_id;
    status->st_mode = S_IFREG | (mode_t)(info->mode & 07777U);
    status->st_nlink = 1;
    status->st_uid = info->uid;
    status->st_gid = info->gid;
    status->st_size = (off_t)info->byte_size;
}

static void fill_terminal_stat(const NevaTtyMetadataV1* metadata, struct stat* status) {
    memset(status, 0, sizeof(*status));
    status->st_mode = S_IFCHR | metadata->mode;
    status->st_uid = metadata->uid;
    status->st_gid = metadata->gid;
    status->st_ino = (ino_t)metadata->terminal_id;
    status->st_nlink = 1;
}

static int pty_path_identity(const char* path, uint64_t* identity) {
    const char* digit = path + 9;
    *identity = 0;
    if (!*digit) { errno = ENOENT; return -1; }
    for (; *digit; digit++) {
        if (*digit < '0' || *digit > '9' || *identity > (UINT64_MAX - (uint32_t)(*digit - '0')) / 10U) {
            errno = ENOENT; return -1;
        }
        *identity = *identity * 10U + (uint32_t)(*digit - '0');
    }
    if (!*identity) { errno = ENOENT; return -1; }
    return 0;
}

int fstat(int descriptor, struct stat* status) {
    SiltOpenDescription* description = descriptor_get(descriptor);
    if (!description) return -1;
    if (!status) {
        errno = EFAULT;
        return -1;
    }
    memset(status, 0, sizeof(*status));
    if (description->kind == SILT_DESCRIPTION_PTY_SLAVE || description->kind == SILT_DESCRIPTION_PTY_MASTER) {
        NevaTtyMetadataV1 metadata;
        NevaStatus result = sys_service_call(description->handle,
            description->kind == SILT_DESCRIPTION_PTY_MASTER ? PTY_MASTER_RPC_GET_METADATA
                : PTY_SLAVE_RPC_GET_METADATA, (uintptr_t)&metadata, 0, 0, sizeof(metadata),
            NEVA_DEADLINE_INFINITE).status;
        if (result != NEVA_STATUS_OK) { errno = status_errno(result); return -1; }
        fill_terminal_stat(&metadata, status);
        return 0;
    }
    if (description->kind == SILT_DESCRIPTION_TTY || description->kind == SILT_DESCRIPTION_NULL) {
        status->st_mode = S_IFCHR | 0666;
        status->st_nlink = 1;
        return 0;
    }
    if (description->kind == SILT_DESCRIPTION_DIRECTORY) {
        NevaDirectoryInfoV1 info;
        NevaServiceResult result = sys_service_call(
            description->handle, DIRECTORY_RPC_QUERY,
            (uint64_t)(uintptr_t)&info, 0, 0, sizeof(info),
            NEVA_DEADLINE_INFINITE);
        if (result.status != NEVA_STATUS_OK) {
            errno = status_errno(result.status);
            return -1;
        }
        status->st_ino = (ino_t)info.object_id;
        status->st_dev = (dev_t)info.volume_id;
        status->st_mode = S_IFDIR | 0555;
        status->st_nlink = 1;
        return 0;
    }
    if (description->kind == SILT_DESCRIPTION_PIPE_READ || description->kind == SILT_DESCRIPTION_PIPE_WRITE) {
        status->st_mode = S_IFIFO | 0600;
        status->st_nlink = 1;
        return 0;
    }
    NevaRemoteFileInfoV1 info;
    if (query_file(description->handle, &info) < 0) return -1;
    fill_file_stat(&info, status);
    return 0;
}

int stat(const char* path, struct stat* status) {
    if (!status) {
        errno = EFAULT;
        return -1;
    }
    char normalized[NEVA_FS_PATH_MAX + 1U];
    if (normalize_path(path, normalized) < 0) return -1;
    if (strcmp(normalized, "/") == 0) {
        memset(status, 0, sizeof(*status));
        // Device zero is synthetic. Reserve inode one for the virtual root,
        // distinct from the zero identity of synthetic character devices.
        status->st_ino = 1;
        status->st_mode = S_IFDIR | 0555;
        status->st_nlink = 1;
        return 0;
    }
    if (strncmp(normalized, "/dev/pts/", 9) == 0) {
        uint64_t identity;
        NevaTtyMetadataV1 metadata;
        if (pty_path_identity(normalized, &identity) || silt_pty_metadata(identity, &metadata)) return -1;
        fill_terminal_stat(&metadata, status);
        return 0;
    }
    if (strcmp(normalized, "/dev/ptmx") == 0 || strcmp(normalized, "/dev/null") == 0
        || strcmp(normalized, "/dev/tty") == 0) {
        memset(status, 0, sizeof(*status));
        status->st_mode = S_IFCHR | 0666;
        status->st_nlink = 1;
        return 0;
    }
    // Query directories first. Both remote interfaces use method 1 and a
    // 64-byte query structure, so method-number dispatch alone cannot identify
    // the capability family safely.
    NevaServiceResult result = resolve_path(
        normalized, NEVA_RESOLVE_FLAG_DIRECTORY, HANDLE_RIGHT_METADATA_READ);
    if (result.status == NEVA_STATUS_OK && result.handle) {
        NevaDirectoryInfoV1 info = { 0 };
        NevaServiceResult queried = sys_service_call(
            result.handle, DIRECTORY_RPC_QUERY, (uint64_t)(uintptr_t)&info,
            0, 0, sizeof(info), NEVA_DEADLINE_INFINITE);
        (void)sys_handle_close(result.handle);
        if (queried.status == NEVA_STATUS_OK && info.magic == NEVA_DIRECTORY_INFO_MAGIC
            && info.version == NEVA_FILESYSTEM_ABI_VERSION && info.size == sizeof(info)) {
            memset(status, 0, sizeof(*status));
            status->st_ino = (ino_t)info.object_id;
            status->st_dev = (dev_t)info.volume_id;
            status->st_mode = S_IFDIR | 0555;
            status->st_nlink = 1;
            return 0;
        }
    }
    result = resolve_path(normalized, 0, HANDLE_RIGHT_METADATA_READ);
    if (result.status != NEVA_STATUS_OK || !result.handle) {
        errno = status_errno(result.status);
        return -1;
    }
    NevaRemoteFileInfoV1 info;
    int queried = query_file(result.handle, &info);
    (void)sys_handle_close(result.handle);
    if (queried < 0) return -1;
    fill_file_stat(&info, status);
    return 0;
}

int lstat(const char* path, struct stat* status) {
    return stat(path, status);
}

char* getcwd(char* buffer, size_t size) {
    return silt_copy_cwd(g_cwd, buffer, size);
}

int chdir(const char* path) {
    char normalized[NEVA_FS_PATH_MAX + 1U];
    if (normalize_path(path, normalized) < 0) return -1;
    if (strcmp(normalized, "/") == 0) {
        strcpy(g_cwd, normalized);
        return 0;
    }
    NevaServiceResult result = resolve_path(
        normalized, NEVA_RESOLVE_FLAG_DIRECTORY,
        HANDLE_RIGHT_LOOKUP | HANDLE_RIGHT_METADATA_READ);
    if (result.status != NEVA_STATUS_OK || !result.handle) {
        errno = status_errno(result.status);
        return -1;
    }
    NevaDirectoryInfoV1 info = { 0 };
    NevaServiceResult queried = sys_service_call(result.handle, DIRECTORY_RPC_QUERY,
        (uintptr_t)&info, 0, 0, sizeof(info), NEVA_DEADLINE_INFINITE);
    (void)sys_handle_close(result.handle);
    if (queried.status != NEVA_STATUS_OK || info.magic != NEVA_DIRECTORY_INFO_MAGIC
        || info.version != NEVA_FILESYSTEM_ABI_VERSION || info.size != sizeof(info)) {
        errno = queried.status != NEVA_STATUS_OK ? status_errno(queried.status) : ENOTDIR;
        return -1;
    }
    strcpy(g_cwd, normalized);
    return 0;
}

DIR* opendir(const char* path) {
    char normalized[NEVA_FS_PATH_MAX + 1U];
    if (normalize_path(path, normalized) < 0) return NULL;
    NevaServiceResult result = resolve_path(
        normalized, NEVA_RESOLVE_FLAG_DIRECTORY,
        HANDLE_RIGHT_LOOKUP | HANDLE_RIGHT_ENUMERATE | HANDLE_RIGHT_METADATA_READ);
    if (result.status != NEVA_STATUS_OK || !result.handle) {
        errno = status_errno(result.status);
        return NULL;
    }
    DIR* directory = malloc(sizeof(*directory));
    if (!directory) {
        (void)sys_handle_close(result.handle);
        errno = ENOMEM;
        return NULL;
    }
    memset(directory, 0, sizeof(*directory));
    directory->handle = result.handle;
    directory->shm = sys_shm_create(SILT_IO_BYTES);
    directory->mapping = directory->shm
        ? (uint8_t*)sys_shm_map(directory->shm) : (void*)-1;
    uint32_t transfer = directory->mapping != (void*)-1 ? sys_handle_dup(
        directory->shm,
        HANDLE_RIGHT_READ | HANDLE_RIGHT_WRITE | HANDLE_RIGHT_TRANSFER) : 0;
    NevaDirectoryEnumerateV1 request = {
        .magic = NEVA_DIRECTORY_ENUM_MAGIC,
        .version = NEVA_FILESYSTEM_ABI_VERSION,
        .size = sizeof(request),
        .entry_capacity = NEVA_FS_ENUM_MAX,
        .data_region_handle = transfer,
        .data_length = SILT_IO_BYTES,
        .flags = NEVA_ENUM_FLAG_RESTART,
    };
    NevaServiceResult enumerated = transfer ? sys_service_call(
        directory->handle, DIRECTORY_RPC_ENUMERATE,
        (uint64_t)(uintptr_t)&request, 0, transfer, sizeof(request),
        NEVA_DEADLINE_INFINITE)
        : (NevaServiceResult){ .status = NEVA_STATUS_NO_MEMORY };
    if (transfer) (void)sys_handle_close(transfer);
    if (enumerated.status != NEVA_STATUS_OK) {
        errno = status_errno(enumerated.status);
        (void)closedir(directory);
        return NULL;
    }
    directory->count = request.entry_count;
    return directory;
}

struct dirent* readdir(DIR* directory) {
    if (!directory) {
        errno = EBADF;
        return NULL;
    }
    if (directory->index >= directory->count) return NULL;
    NevaDirectoryEntryV1* entries = (NevaDirectoryEntryV1*)directory->mapping;
    NevaDirectoryEntryV1* entry = &entries[directory->index++];
    if (entry->name_length > NEVA_FS_COMPONENT_MAX
        || entry->name_offset > SILT_IO_BYTES
        || entry->name_length > SILT_IO_BYTES - entry->name_offset) {
        errno = EIO;
        return NULL;
    }
    memset(&directory->current, 0, sizeof(directory->current));
    directory->current.d_ino = entry->object_id;
    directory->current.d_type = entry->kind == NEVA_FS_KIND_DIRECTORY
        ? DT_DIR : entry->kind == NEVA_FS_KIND_FILE ? DT_REG
        : entry->kind == NEVA_FS_KIND_SYMLINK ? DT_LNK : DT_UNKNOWN;
    memcpy(directory->current.d_name, directory->mapping + entry->name_offset,
           entry->name_length);
    directory->current.d_name[entry->name_length] = '\0';
    return &directory->current;
}

int closedir(DIR* directory) {
    if (!directory) {
        errno = EBADF;
        return -1;
    }
    if (directory->mapping && directory->mapping != (void*)-1) {
        (void)sys_shm_unmap(directory->shm);
    } else if (directory->shm) (void)sys_handle_close(directory->shm);
    if (directory->handle) (void)sys_handle_close(directory->handle);
    free(directory);
    return 0;
}

void rewinddir(DIR* directory) {
    if (directory) directory->index = 0;
}

int posix_openpt(int flags) { return open("/dev/ptmx", flags); }

static uint32_t pty_master_handle(int descriptor) {
    SiltOpenDescription* description = descriptor_get(descriptor);
    if (!description) return 0;
    if (description->kind != SILT_DESCRIPTION_PTY_MASTER) { errno = EINVAL; return 0; }
    return description->handle;
}

int grantpt(int descriptor) {
    uint32_t master = pty_master_handle(descriptor);
    if (!master) return -1;
    SiltCleanup cleanup;
    uint32_t group = silt_group_acquire_guarded(&cleanup, 0);
    NevaStatus status = sys_service_call(master, PTY_MASTER_RPC_GRANT,
        0, 0, group, 0, NEVA_DEADLINE_INFINITE).status;
    silt_cleanup_end(&cleanup);
    if (status != NEVA_STATUS_OK) { errno = EACCES; return -1; }
    return 0;
}

int unlockpt(int descriptor) {
    uint32_t master = pty_master_handle(descriptor);
    if (!master) return -1;
    if (sys_service_call(master, PTY_MASTER_RPC_UNLOCK, 0, 0, 0, 0,
        NEVA_DEADLINE_INFINITE).status != NEVA_STATUS_OK) { errno = EIO; return -1; }
    return 0;
}

char* ptsname(int descriptor) {
    static char name[40];
    uint32_t master = pty_master_handle(descriptor);
    if (!master) return NULL;
    NevaServiceResult result = sys_service_call(master, PTY_MASTER_RPC_GET_SLAVE_ID,
        0, 0, 0, 0, NEVA_DEADLINE_INFINITE);
    if ((int64_t)result.value <= 0) { errno = EIO; return NULL; }
    snprintf(name, sizeof(name), "/dev/pts/%llu", (unsigned long long)result.value);
    return name;
}
