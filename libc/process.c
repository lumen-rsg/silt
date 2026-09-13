#include "libneva.h"
#include "session_control.h"
#include "filesystem_service.h"
#include "silt_internal.h"
#include "silt_pipeline.h"

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define SILT_CHILD_MAX 16
#define SILT_EXEC_ARGUMENT_MAX 16
#define SILT_EXEC_BYTES_MAX 127U

typedef struct {
    pid_t pid;
    uint32_t process;
    uint8_t suspended;
    uint8_t constructing;
    pid_t pgid;
    uint32_t group;
} SiltChild;

static SiltChild g_children[SILT_CHILD_MAX];
static uint32_t g_pipeline_group;
static pid_t g_pipeline_leader;
static int g_pipeline_active;
static int g_pipeline_foreground;
static int g_job_enabled;
static int g_job_foreground;

static void child_remove(int index);
static int child_index(pid_t pid);

void silt_job_prepare(int group, int foreground) {
    g_job_enabled = group >= 0;
    g_job_foreground = foreground;
}

int silt_job_finish(int pid) {
    int index = child_index(pid);
    if (index < 0) { errno = ECHILD; return -1; }
    if (g_job_enabled && g_job_foreground && g_children[index].group) {
        // Keep the shell foreground until every suspended stage is admitted.
        // Otherwise a later fork failure leaves the prompt in a dead group.
        if (g_pipeline_active) {
            g_pipeline_foreground = 1;
        } else if (silt_controlling_set_foreground(g_children[index].group)
                   != NEVA_STATUS_OK) {
            silt_pipeline_abort();
            errno = EIO;
            return -1;
        }
    }
    int result = g_pipeline_active ? 0 : silt_pipeline_end();
    g_job_enabled = 0;
    return result;
}

void silt_pipeline_begin(void) {
    if (g_pipeline_active || g_pipeline_group) silt_pipeline_abort();
    g_pipeline_active = 1;
    g_pipeline_foreground = 0;
}

int silt_pipeline_end(void) {
    if (g_pipeline_foreground && g_pipeline_group
        && silt_controlling_set_foreground(g_pipeline_group) != NEVA_STATUS_OK) {
        silt_pipeline_abort();
        errno = EIO;
        return -1;
    }
    for (int index = 0; index < SILT_CHILD_MAX; index++) {
        SiltChild* child = &g_children[index];
        if (!child->process || !child->constructing || !child->suspended) continue;
        if ((NevaStatus)(int64_t)sys_rpc(child->process, PROCESS_RPC_RESUME, 0, 0)
            != NEVA_STATUS_OK) {
            silt_pipeline_abort();
            errno = EAGAIN;
            return -1;
        }
        child->suspended = 0;
        // A resumed prefix still belongs to construction until every stage
        // starts. A later refusal must reclaim it as well as suspended peers.
    }
    for (int index = 0; index < SILT_CHILD_MAX; index++) {
        g_children[index].constructing = 0;
    }
    g_pipeline_foreground = 0;
    g_pipeline_active = 0;
    if (g_pipeline_group) (void)sys_handle_close(g_pipeline_group);
    g_pipeline_group = NEVA_INVALID_HANDLE;
    g_pipeline_leader = 0;
    return 0;
}

void silt_pipeline_abort(void) {
    g_pipeline_active = 0;
    g_pipeline_foreground = 0;
    g_job_enabled = 0;
    // Stop every construction member before waiting for any one member.
    // Existing jobs have cleared constructing and retain their capabilities.
    for (int index = 0; index < SILT_CHILD_MAX; index++) {
        SiltChild* child = &g_children[index];
        if (child->process && child->constructing) {
            (void)sys_rpc(child->process, PROCESS_RPC_TERMINATE, 127, 0);
        }
    }
    for (int index = 0; index < SILT_CHILD_MAX; index++) {
        SiltChild* child = &g_children[index];
        if (!child->process || !child->constructing) continue;
        NevaWaitResult waited;
        do {
            waited = sys_wait_capability(
                child->process, WAIT_REPORT_EXITED, NEVA_DEADLINE_INFINITE);
        } while (waited.status == NEVA_STATUS_INTERRUPTED);
        child_remove(index);
    }
    if (g_pipeline_group) (void)sys_handle_close(g_pipeline_group);
    g_pipeline_group = NEVA_INVALID_HANDLE;
    g_pipeline_leader = 0;
}

static void children_clear_in_child(void) {
    for (int index = 0; index < SILT_CHILD_MAX; index++) {
        if (g_children[index].process) {
            (void)sys_handle_close(g_children[index].process);
        }
        if (g_children[index].group) (void)sys_handle_close(g_children[index].group);
        g_children[index] = (SiltChild){ 0 };
    }
    if (g_pipeline_group) (void)sys_handle_close(g_pipeline_group);
    g_pipeline_group = NEVA_INVALID_HANDLE;
    g_pipeline_leader = 0;
    g_pipeline_active = 0;
    g_pipeline_foreground = 0;
    g_job_enabled = 0;
}

static int pipeline_attach(uint32_t process, pid_t pid) {
    NevaStatus status = (NevaStatus)(int64_t)sys_rpc(process, PROCESS_RPC_SETPGID,
        g_pipeline_group ? (uint64_t)g_pipeline_leader : 0, 0);
    if (status != NEVA_STATUS_OK) return -1;
    if (!g_pipeline_group) {
        int64_t group = (int64_t)sys_rpc(process, PROCESS_RPC_DUP_GROUP, 0, 0);
        if (group <= 0 || group > UINT32_MAX) return -1;
        g_pipeline_group = (uint32_t)group;
        g_pipeline_leader = pid;
    }
    return 0;
}

static int child_insert(pid_t pid, uint32_t process, int suspended) {
    for (int index = 0; index < SILT_CHILD_MAX; index++) {
        if (g_children[index].pid != 0) continue;
        uint32_t retained_group = g_pipeline_group ? sys_handle_dup(g_pipeline_group,
            HANDLE_RIGHT_RPC | HANDLE_RIGHT_INSPECT | HANDLE_RIGHT_SIGNAL
                | HANDLE_RIGHT_TRANSFER) : 0;
        if (g_pipeline_group && !retained_group) return -1;
        g_children[index] = (SiltChild){
            .pid = pid,
            .process = process,
            .suspended = (uint8_t)suspended,
            .constructing = (uint8_t)suspended,
            .pgid = g_pipeline_leader,
            .group = retained_group,
        };
        return 0;
    }
    return -1;
}

static int child_index(pid_t pid) {
    for (int index = 0; index < SILT_CHILD_MAX; index++) {
        if (g_children[index].pid == pid) return index;
    }
    return -1;
}

static void child_remove(int index) {
    if (index < 0 || index >= SILT_CHILD_MAX) return;
    if (g_children[index].process) {
        (void)sys_handle_close(g_children[index].process);
    }
    if (g_children[index].group) (void)sys_handle_close(g_children[index].group);
    g_children[index] = (SiltChild){ 0 };
}

pid_t fork(void) {
    NevaForkResult result = sys_fork_capability_flags(
        NEVA_FORK_START_SUSPENDED);
    if (result.child_pid == 0) {
        children_clear_in_child();
        return 0;
    }
    if (result.child_pid < 0 || !result.child_process_handle) {
        if (result.child_process_handle) {
            (void)sys_handle_close(result.child_process_handle);
        }
        errno = EAGAIN;
        return -1;
    }
    if ((g_pipeline_active || g_job_enabled)
        && pipeline_attach(
               result.child_process_handle, (pid_t)result.child_pid) < 0) {
        (void)sys_rpc(
            result.child_process_handle, PROCESS_RPC_TERMINATE, 127, 0);
        NevaWaitResult waited = sys_wait_capability(
            result.child_process_handle, WAIT_REPORT_EXITED,
            NEVA_DEADLINE_INFINITE);
        (void)waited;
        (void)sys_handle_close(result.child_process_handle);
        errno = EAGAIN;
        return -1;
    }
    if (result.child_pid > INT32_MAX
        || child_insert((pid_t)result.child_pid,
                        result.child_process_handle, g_pipeline_active || g_job_enabled) < 0) {
        (void)sys_rpc(result.child_process_handle, PROCESS_RPC_TERMINATE, 127, 0);
        NevaWaitResult waited = sys_wait_capability(
            result.child_process_handle, WAIT_REPORT_EXITED,
            NEVA_DEADLINE_INFINITE);
        (void)waited;
        (void)sys_handle_close(result.child_process_handle);
        errno = EAGAIN;
        return -1;
    }
    if (!g_pipeline_active && !g_job_enabled
        && (NevaStatus)(int64_t)sys_rpc(
            result.child_process_handle, PROCESS_RPC_RESUME, 0, 0)
        != NEVA_STATUS_OK) {
        int index = child_index((pid_t)result.child_pid);
        (void)sys_rpc(
            result.child_process_handle, PROCESS_RPC_TERMINATE, 127, 0);
        NevaWaitResult waited = sys_wait_capability(
            result.child_process_handle, WAIT_REPORT_EXITED,
            NEVA_DEADLINE_INFINITE);
        (void)waited;
        if (index >= 0) child_remove(index);
        errno = EAGAIN;
        return -1;
    }
    return (pid_t)result.child_pid;
}

pid_t vfork(void) {
    // dash immediately execs or exits on this path. A regular COW fork keeps
    // the parent address space private while satisfying that contract.
    return fork();
}

static int build_argument_vector(char* const arguments[], char* bytes,
                                 size_t* byte_count, uint16_t* argument_count) {
    if (!arguments || !bytes || !byte_count || !argument_count) return -1;
    size_t cursor = 0;
    uint16_t count = 0;
    while (arguments[count]) {
        if (count == SILT_EXEC_ARGUMENT_MAX) return -1;
        size_t length = strlen(arguments[count]) + 1U;
        if (length > SILT_EXEC_BYTES_MAX - cursor) return -1;
        memcpy(bytes + cursor, arguments[count], length);
        cursor += length;
        count++;
    }
    if (count == 0) return -1;
    *byte_count = cursor;
    *argument_count = count;
    return 0;
}

int execve(const char* path, char* const arguments[], char* const environment[]) {
    if (!path || !arguments) {
        errno = EFAULT;
        return -1;
    }
    char bytes[SILT_EXEC_BYTES_MAX];
    size_t byte_count = 0;
    uint16_t argument_count = 0;
    if (build_argument_vector(
            arguments, bytes, &byte_count, &argument_count) < 0) {
        errno = E2BIG;
        return -1;
    }
    // Iterative rewriting bounds interpreter chains without recursive stack
    // growth. Every hop preserves argv[1..], including empty arguments.
    char rewritten[SILT_EXEC_BYTES_MAX];
    char current_path[NEVA_FS_PATH_MAX + 1U];
    if (strlen(path) >= sizeof(current_path)) { errno = ENAMETOOLONG; return -1; }
    strcpy(current_path, path);
    SiltCleanup executable_guard;
    uint32_t executable = 0;
    for (unsigned depth = 0; ; depth++) {
        char header[SILT_SHEBANG_BYTES] = { 0 };
        uint32_t mask = silt_cleanup_begin(&executable_guard);
        executable = silt_cleanup_adopt(&executable_guard, silt_resolve_executable(current_path, header));
        int saved_errno = errno;
        silt_cleanup_ready(mask);
        if (executable) break;
        silt_cleanup_end(&executable_guard);
        errno = saved_errno;
        if (errno != ENOEXEC || header[0] != '#' || header[1] != '!') return -1;
        if (depth == 4) { errno = ELOOP; return -1; }
        size_t end = 2;
        while (end < sizeof(header) && header[end] && header[end] != '\n') end++;
        if (end == sizeof(header) || header[end] != '\n') { errno = ENOEXEC; return -1; }
        while (end > 2 && (header[end - 1] == ' ' || header[end - 1] == '\t')) end--;
        header[end] = 0;
        char* interpreter = header + 2;
        while (*interpreter == ' ' || *interpreter == '\t') interpreter++;
        char* option = interpreter;
        while (*option && *option != ' ' && *option != '\t') option++;
        if (*option) *option++ = 0;
        while (*option == ' ' || *option == '\t') option++;
        if (*interpreter != '/') { errno = ENOEXEC; return -1; }
        char* next_arguments[SILT_EXEC_ARGUMENT_MAX + 1];
        unsigned next_count = 0;
        next_arguments[next_count++] = interpreter;
        if (*option) next_arguments[next_count++] = option;
        next_arguments[next_count++] = current_path;
        size_t cursor = strlen(bytes) + 1U;
        for (unsigned index = 1; index < argument_count; index++) {
            if (next_count == SILT_EXEC_ARGUMENT_MAX) { errno = E2BIG; return -1; }
            next_arguments[next_count++] = bytes + cursor;
            cursor += strlen(bytes + cursor) + 1U;
        }
        next_arguments[next_count] = NULL;
        char* next = rewritten;
        if (build_argument_vector(next_arguments, next, &byte_count, &argument_count) < 0) {
            errno = E2BIG; return -1;
        }
        strcpy(current_path, interpreter);
        memcpy(bytes, next, byte_count);
    }
    SiltExecInfoV3 info;
    memset(&info, 0, sizeof(info));
    info.magic = SILT_EXEC_INFO_MAGIC;
    info.version = SILT_EXEC_INFO_VERSION;
    info.total_size = sizeof(info);
    if (environment) {
        while (environment[info.environment_count]) {
            if (info.environment_count == SILT_EXEC_ENVIRONMENT_MAX) {
                silt_cleanup_end(&executable_guard);
                errno = E2BIG;
                return -1;
            }
            size_t length = strlen(environment[info.environment_count]);
            SiltExecStringV2* entry =
                &info.environment[info.environment_count];
            if (length > UINT16_MAX
                || silt_exec_string_append(
                       &info, environment[info.environment_count], length,
                       &entry->offset) < 0) {
                silt_cleanup_end(&executable_guard);
                errno = E2BIG;
                return -1;
            }
            entry->length = (uint16_t)length;
            info.environment_count++;
        }
    }
    if (silt_descriptors_exec_export(&info) < 0) {
        silt_cleanup_end(&executable_guard);
        errno = E2BIG;
        return -1;
    }
    int result = sys_exec_image(
        executable, bytes, byte_count, argument_count, &info, sizeof(info));
    silt_cleanup_end(&executable_guard);
    errno = result == NEVA_STATUS_ACCESS_DENIED ? EACCES
        : result == NEVA_STATUS_NOT_FOUND ? ENOENT : ENOEXEC;
    return -1;
}

static int wait_status(const NevaWaitResult* result) {
    if (result->kind == WAIT_KIND_STOPPED) {
        return ((result->exit_status & 0xff) << 8) | 0x7f;
    }
    if (result->kind == WAIT_KIND_CONTINUED) return 0xffff;
    if (result->termination_signal) return (int)result->termination_signal & 0x7f;
    return (result->exit_status & 0xff) << 8;
}

static pid_t wait_child(int index, int* status, int options) {
    uint32_t wait_options = WAIT_REPORT_EXITED;
    if (options & WUNTRACED) wait_options |= WAIT_REPORT_STOPPED;
    if (options & WCONTINUED) wait_options |= WAIT_REPORT_CONTINUED;
    if (options & WNOHANG) wait_options |= WAIT_NOHANG;
    NevaWaitResult result = sys_wait_capability(
        g_children[index].process, wait_options,
        (options & WNOHANG) ? 0 : NEVA_DEADLINE_INFINITE);
    if (result.status == NEVA_STATUS_WOULD_BLOCK
        || result.status == NEVA_STATUS_TIMED_OUT) return 0;
    if (result.status != NEVA_STATUS_OK) {
        errno = result.status == NEVA_STATUS_NO_CHILD ? ECHILD
            : result.status == NEVA_STATUS_INTERRUPTED ? EINTR : EIO;
        return -1;
    }
    pid_t pid = (pid_t)result.child_pid;
    if (status) *status = wait_status(&result);
    if (result.kind == WAIT_KIND_EXITED) child_remove(index);
    return pid;
}

pid_t waitpid(pid_t process, int* status, int options) {
    if ((options & ~(WNOHANG | WUNTRACED | WCONTINUED)) != 0 || process == 0
        || process < -1) {
        errno = EINVAL;
        return -1;
    }
    if (process > 0) {
        int index = child_index(process);
        if (index < 0) {
            errno = ECHILD;
            return -1;
        }
        return wait_child(index, status, options);
    }
    int found = 0;
    for (int index = 0; index < SILT_CHILD_MAX; index++) {
        if (g_children[index].pid == 0) continue;
        found = 1;
        pid_t result = wait_child(index, status, options | WNOHANG);
        if (result != 0) return result;
    }
    if (!found) {
        errno = ECHILD;
        return -1;
    }
    if (options & WNOHANG) return 0;
    // The zero selector is confined by Neva to the caller's direct children.
    // Blocking on one arbitrary child would hide another job's stop or exit.
    NevaWaitResult result = sys_wait_raw(0, WAIT_REPORT_EXITED
        | ((options & WUNTRACED) ? WAIT_REPORT_STOPPED : 0)
        | ((options & WCONTINUED) ? WAIT_REPORT_CONTINUED : 0), NEVA_DEADLINE_INFINITE);
    if (result.status != NEVA_STATUS_OK) {
        errno = result.status == NEVA_STATUS_INTERRUPTED ? EINTR : ECHILD;
        return -1;
    }
    if (status) *status = wait_status(&result);
    if (result.kind == WAIT_KIND_EXITED) child_remove(child_index((pid_t)result.child_pid));
    return (pid_t)result.child_pid;
}

pid_t wait(int* status) {
    return waitpid(-1, status, 0);
}

pid_t wait3(int* status, int options, struct rusage* usage) {
    if (usage) memset(usage, 0, sizeof(*usage));
    return waitpid(-1, status, options);
}

int kill(pid_t process, int signal_number) {
    if (process <= 0 && process != -1 && process != INT32_MIN) {
        return killpg(-process, signal_number);
    }
    if (signal_number < 0 || signal_number >= NSIG || process <= 0) {
        errno = process <= 0 ? ENOSYS : EINVAL;
        return -1;
    }
    if (process == getpid()) {
        NevaStartupHandleV1 record;
        if (neva_startup_find("process", &record) != NEVA_STATUS_OK) {
            errno = ESRCH;
            return -1;
        }
        NevaStatus result = (NevaStatus)(int64_t)sys_rpc(
            record.handle, PROCESS_RPC_SIGNAL, (uint64_t)signal_number, 0);
        if (result != NEVA_STATUS_OK) {
            errno = result == NEVA_STATUS_INVALID_ARGUMENT ? EINVAL : ESRCH;
            return -1;
        }
        return 0;
    }
    int index = child_index(process);
    if (index < 0) {
        int result = sys_kill((uint32_t)process, signal_number);
        if (result == 0) return 0;
        errno = result == NEVA_STATUS_ACCESS_DENIED ? EPERM
            : result == NEVA_STATUS_INVALID_ARGUMENT ? EINVAL : ESRCH;
        return -1;
    }
    NevaStatus result = (NevaStatus)(int64_t)sys_rpc(
        g_children[index].process, PROCESS_RPC_SIGNAL,
        (uint64_t)signal_number, 0);
    if (result != NEVA_STATUS_OK) {
        errno = result == NEVA_STATUS_INVALID_ARGUMENT ? EINVAL : ESRCH;
        return -1;
    }
    return 0;
}

pid_t getppid(void) {
    NevaStartupHandleV1 record;
    NevaProcessInfoV1 info;
    if (neva_startup_find("process", &record) != NEVA_STATUS_OK
        || (NevaStatus)(int64_t)sys_rpc(
               record.handle, PROCESS_RPC_QUERY,
               (uint64_t)(uintptr_t)&info, sizeof(info)) != NEVA_STATUS_OK) {
        return 0;
    }
    return (pid_t)info.parent_process_id;
}

static uint32_t posix_process_handle(pid_t process) {
    if (process == 0 || process == getpid()) {
        NevaStartupHandleV1 record;
        return neva_startup_find("process", &record) == NEVA_STATUS_OK ? record.handle : 0;
    }
    int index = child_index(process);
    return index >= 0 ? g_children[index].process : 0;
}

// Only called while the guarded wrapper has blocked signal delivery. Query
// actual membership: a child may have changed group/session since fork.
static uint32_t silt_group_acquire(int process_group) {
    uint32_t process = posix_process_handle(0);
    if (process_group && (int64_t)sys_rpc(process, PROCESS_RPC_GETPGID, 0, 0) != process_group) {
        process = 0;
        for (int index = 0; index < SILT_CHILD_MAX; index++) {
            uint32_t child = g_children[index].process;
            if (child && (int64_t)sys_rpc(child, PROCESS_RPC_GETPGID, 0, 0) == process_group) {
                process = child;
                break;
            }
        }
    }
    uint32_t selector = process ? 0 : (uint32_t)process_group;
    if (!process) process = posix_process_handle(0);
    int64_t group = process ? (int64_t)sys_rpc(process, PROCESS_RPC_DUP_GROUP, selector, 0) : 0;
    return group > 0 && group <= UINT32_MAX ? (uint32_t)group : 0;
}

uint32_t silt_group_acquire_guarded(SiltCleanup* cleanup, int process_group) {
    uint32_t mask = silt_cleanup_begin(cleanup);
    (void)silt_cleanup_adopt(cleanup, silt_group_acquire(process_group));
    silt_cleanup_ready(mask);
    return cleanup->handle;
}

static int process_group_error(int64_t status) {
    errno = status == NEVA_STATUS_ACCESS_DENIED ? EPERM
        : status == NEVA_STATUS_BUSY ? EACCES
        : status == NEVA_STATUS_INVALID_ARGUMENT ? EINVAL
        : status == NEVA_STATUS_NO_MEMORY || status == NEVA_STATUS_LIMIT_REACHED ? EAGAIN
        : ESRCH;
    return -1;
}

pid_t getpgid(pid_t process) {
    uint32_t handle = process >= 0 ? posix_process_handle(process) : 0;
    uint32_t selector = handle ? 0 : (uint32_t)process;
    if (!handle && process > 0) handle = posix_process_handle(0);
    if (!handle) { errno = ESRCH; return -1; }
    int64_t result = (int64_t)sys_rpc(handle, PROCESS_RPC_GETPGID, selector, 0);
    return result > 0 ? (pid_t)result : process_group_error(result);
}

pid_t getsid(pid_t process) {
    uint32_t handle = process >= 0 ? posix_process_handle(process) : 0;
    uint32_t selector = handle ? 0 : (uint32_t)process;
    if (!handle && process > 0) handle = posix_process_handle(0);
    if (!handle) { errno = ESRCH; return -1; }
    int64_t result = (int64_t)sys_rpc(handle, PROCESS_RPC_GETSID, selector, 0);
    return result > 0 ? (pid_t)result : process_group_error(result);
}

pid_t setsid(void) {
    uint32_t handle = posix_process_handle(0);
    if (!handle) { errno = ESRCH; return -1; }
    int64_t result = (int64_t)sys_rpc(handle, PROCESS_RPC_SETSID, 0, 0);
    return result > 0 ? (pid_t)result : process_group_error(result);
}

pid_t getpgrp(void) { return getpgid(0); }

int setpgid(pid_t process, pid_t group) {
    if (process < 0 || group < 0) { errno = EINVAL; return -1; }
    uint32_t handle = posix_process_handle(process);
    if (!handle) { errno = ESRCH; return -1; }
    int64_t result = (int64_t)sys_rpc(handle, PROCESS_RPC_SETPGID, (uint64_t)group, 0);
    return result == NEVA_STATUS_OK ? 0 : process_group_error(result);
}

int killpg(pid_t process_group, int signal_number) {
    if (process_group < 0 || signal_number < 0 || signal_number >= NSIG) {
        errno = EINVAL; return -1;
    }
    SiltCleanup cleanup;
    uint32_t group = silt_group_acquire_guarded(&cleanup, process_group);
    if (!group) { silt_cleanup_end(&cleanup); errno = ESRCH; return -1; }
    NevaProcessGroupInfoV1 info;
    NevaStatus status = (NevaStatus)(int64_t)sys_rpc(group, PROCESS_GROUP_RPC_QUERY,
        (uint64_t)(uintptr_t)&info, sizeof(info));
    if (status == NEVA_STATUS_OK) status = (NevaStatus)(int64_t)sys_rpc(group,
        PROCESS_GROUP_RPC_SIGNAL, (uint64_t)signal_number, info.generation);
    silt_cleanup_end(&cleanup);
    if (status == NEVA_STATUS_OK) return 0;
    errno = ESRCH;
    return -1;
}
