#include "libneva.h"
#include "session_control.h"
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
    uint8_t pipe_readers;
    uint8_t pipe_writers;
} SiltChild;

static SiltChild g_children[SILT_CHILD_MAX];
static uint32_t g_pipeline_group;
static pid_t g_pipeline_leader;
static int g_pipeline_active;

static void child_remove(int index);

static uint32_t session_remote(void) {
    NevaStartupHandleV1 record;
    return neva_startup_find("catalog", &record) == NEVA_STATUS_OK
            && record.object_type == NEVA_OBJECT_TYPE_REMOTE_OBJECT
        ? record.handle : NEVA_INVALID_HANDLE;
}

void silt_pipeline_begin(void) {
    if (g_pipeline_active || g_pipeline_group) silt_pipeline_abort();
    g_pipeline_active = 1;
}

static void child_discard_suspended(int index) {
    SiltChild* child = &g_children[index];
    if (!child->process || !child->suspended) return;
    silt_descriptors_fork_discard(
        child->pipe_readers, child->pipe_writers);
    child->pipe_readers = 0;
    child->pipe_writers = 0;
    (void)sys_rpc(child->process, PROCESS_RPC_TERMINATE, 127, 0);
    NevaWaitResult waited = sys_wait_capability(
        child->process, WAIT_REPORT_EXITED, NEVA_DEADLINE_INFINITE);
    (void)waited;
    child_remove(index);
}

void silt_pipeline_end(void) {
    g_pipeline_active = 0;
    for (int index = 0; index < SILT_CHILD_MAX; index++) {
        if (!g_children[index].process || !g_children[index].suspended) continue;
        if ((NevaStatus)(int64_t)sys_rpc(
                g_children[index].process, PROCESS_RPC_RESUME, 0, 0)
            == NEVA_STATUS_OK) {
            silt_descriptors_fork_commit(
                g_children[index].pipe_readers,
                g_children[index].pipe_writers);
            g_children[index].suspended = 0;
            g_children[index].pipe_readers = 0;
            g_children[index].pipe_writers = 0;
        } else {
            child_discard_suspended(index);
        }
    }
    if (g_pipeline_group) (void)sys_handle_close(g_pipeline_group);
    g_pipeline_group = NEVA_INVALID_HANDLE;
    g_pipeline_leader = 0;
}

void silt_pipeline_abort(void) {
    g_pipeline_active = 0;
    for (int index = 0; index < SILT_CHILD_MAX; index++) {
        child_discard_suspended(index);
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
        g_children[index] = (SiltChild){ 0 };
    }
    if (g_pipeline_group) (void)sys_handle_close(g_pipeline_group);
    g_pipeline_group = NEVA_INVALID_HANDLE;
    g_pipeline_leader = 0;
    g_pipeline_active = 0;
}

static int pipeline_attach(uint32_t process, pid_t pid) {
    uint32_t remote = session_remote();
    uint32_t process_transfer = remote ? sys_handle_dup(
        process, HANDLE_RIGHT_RPC | HANDLE_RIGHT_ADMIN
            | HANDLE_RIGHT_INSPECT | HANDLE_RIGHT_TRANSFER) : 0;
    if (!process_transfer) return -1;
    NevaServiceResult result;
    if (!g_pipeline_group) {
        result = sys_service_call(
            remote, SESSION_CONTROL_RPC_CREATE_JOB, 0, 0,
            process_transfer, 0, NEVA_DEADLINE_INFINITE);
        (void)sys_handle_close(process_transfer);
        if (result.status != NEVA_STATUS_OK || !result.handle) return -1;
        g_pipeline_group = result.handle;
        g_pipeline_leader = pid;
    } else {
        result = sys_service_call(
            remote, SESSION_CONTROL_RPC_JOIN_JOB,
            (uint64_t)g_pipeline_leader, 0,
            process_transfer, 0, NEVA_DEADLINE_INFINITE);
        (void)sys_handle_close(process_transfer);
        if (result.status != NEVA_STATUS_OK) return -1;
    }
    return 0;
}

static int child_insert(pid_t pid, uint32_t process, int suspended,
                        uint8_t pipe_readers, uint8_t pipe_writers) {
    for (int index = 0; index < SILT_CHILD_MAX; index++) {
        if (g_children[index].pid != 0) continue;
        g_children[index] = (SiltChild){
            .pid = pid,
            .process = process,
            .suspended = (uint8_t)suspended,
            .pipe_readers = pipe_readers,
            .pipe_writers = pipe_writers,
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
    g_children[index] = (SiltChild){ 0 };
}

pid_t fork(void) {
    if (silt_descriptors_fork_prepare() < 0) {
        errno = EAGAIN;
        return -1;
    }
    uint8_t inherited_readers = 0;
    uint8_t inherited_writers = 0;
    silt_descriptors_fork_inheritance(
        &inherited_readers, &inherited_writers);
    NevaForkResult result = sys_fork_capability_flags(
        NEVA_FORK_START_SUSPENDED);
    if (result.child_pid == 0) {
        if (silt_descriptors_fork_child() < 0) _exit(127);
        children_clear_in_child();
        return 0;
    }
    if (result.child_pid < 0 || !result.child_process_handle) {
        silt_descriptors_fork_rollback();
        if (result.child_process_handle) {
            (void)sys_handle_close(result.child_process_handle);
        }
        errno = EAGAIN;
        return -1;
    }
    if (g_pipeline_active
        && pipeline_attach(
               result.child_process_handle, (pid_t)result.child_pid) < 0) {
        silt_descriptors_fork_rollback();
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
                        result.child_process_handle, g_pipeline_active,
                        g_pipeline_active ? inherited_readers : 0,
                        g_pipeline_active ? inherited_writers : 0) < 0) {
        silt_descriptors_fork_rollback();
        (void)sys_rpc(result.child_process_handle, PROCESS_RPC_TERMINATE, 127, 0);
        NevaWaitResult waited = sys_wait_capability(
            result.child_process_handle, WAIT_REPORT_EXITED,
            NEVA_DEADLINE_INFINITE);
        (void)waited;
        (void)sys_handle_close(result.child_process_handle);
        errno = EAGAIN;
        return -1;
    }
    if (!g_pipeline_active
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
        silt_descriptors_fork_rollback();
        errno = EAGAIN;
        return -1;
    }
    if (!g_pipeline_active) {
        silt_descriptors_fork_commit(inherited_readers, inherited_writers);
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
        if (length == 1U || length > SILT_EXEC_BYTES_MAX - cursor) return -1;
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
    uint32_t executable = silt_resolve_executable(path);
    if (!executable) return -1;
    SiltExecInfoV1 info;
    memset(&info, 0, sizeof(info));
    info.magic = SILT_EXEC_INFO_MAGIC;
    info.version = SILT_EXEC_INFO_VERSION;
    info.total_size = sizeof(info);
    if (environment) {
        while (environment[info.environment_count]) {
            if (info.environment_count == SILT_EXEC_ENVIRONMENT_MAX) {
                (void)sys_handle_close(executable);
                errno = E2BIG;
                return -1;
            }
            size_t length = strlen(environment[info.environment_count]);
            SiltExecStringV1* entry =
                &info.environment[info.environment_count];
            if (length > UINT16_MAX
                || silt_exec_string_append(
                       &info, environment[info.environment_count], length,
                       &entry->offset) < 0) {
                (void)sys_handle_close(executable);
                errno = E2BIG;
                return -1;
            }
            entry->length = (uint16_t)length;
            info.environment_count++;
        }
    }
    if (silt_descriptors_exec_export(&info) < 0) {
        (void)sys_handle_close(executable);
        errno = E2BIG;
        return -1;
    }
    int result = sys_exec_image(
        executable, bytes, byte_count, argument_count, &info, sizeof(info));
    (void)sys_handle_close(executable);
    errno = result == NEVA_STATUS_ACCESS_DENIED ? EACCES
        : result == NEVA_STATUS_NOT_FOUND ? ENOENT : ENOEXEC;
    return -1;
}

static int wait_status(const NevaWaitResult* result) {
    if (result->kind == WAIT_KIND_STOPPED) {
        return ((result->exit_status & 0xff) << 8) | 0x7f;
    }
    if (result->kind == WAIT_KIND_CONTINUED) return 0xffff;
    return (result->exit_status & 0xff) << 8;
}

static pid_t wait_child(int index, int* status, int options) {
    uint32_t wait_options = WAIT_REPORT_EXITED;
    if (options & WUNTRACED) wait_options |= WAIT_REPORT_STOPPED;
    if (options & WNOHANG) wait_options |= WAIT_NOHANG;
    NevaWaitResult result = sys_wait_capability(
        g_children[index].process, wait_options,
        (options & WNOHANG) ? 0 : NEVA_DEADLINE_INFINITE);
    if (result.status == NEVA_STATUS_WOULD_BLOCK
        || result.status == NEVA_STATUS_TIMED_OUT) return 0;
    if (result.status != NEVA_STATUS_OK) {
        errno = result.status == NEVA_STATUS_NO_CHILD ? ECHILD : EIO;
        return -1;
    }
    pid_t pid = (pid_t)result.child_pid;
    if (status) *status = wait_status(&result);
    if (result.kind == WAIT_KIND_EXITED) child_remove(index);
    return pid;
}

pid_t waitpid(pid_t process, int* status, int options) {
    if ((options & ~(WNOHANG | WUNTRACED)) != 0 || process == 0
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
    for (int index = 0; index < SILT_CHILD_MAX; index++) {
        if (g_children[index].pid != 0) return wait_child(index, status, options);
    }
    errno = ECHILD;
    return -1;
}

pid_t wait(int* status) {
    return waitpid(-1, status, 0);
}

pid_t wait3(int* status, int options, struct rusage* usage) {
    if (usage) memset(usage, 0, sizeof(*usage));
    return waitpid(-1, status, options);
}

int kill(pid_t process, int signal_number) {
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
        errno = ESRCH;
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
