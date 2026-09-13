#include "libneva.h"
#include "silt_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static int terminal_error(NevaStatus status) {
    errno = status == NEVA_STATUS_INTERRUPTED ? EINTR
        : status == NEVA_STATUS_ACCESS_DENIED ? EACCES : EIO;
    return -1;
}

static NevaStatus terminal_wait_until(uint32_t tty, uint64_t epoch, uint64_t deadline) {
    SiltCleanup cleanup;
    uint32_t mask = silt_cleanup_begin(&cleanup);
    NevaServiceResult event = sys_service_call(tty,
        PTY_SLAVE_RPC_DUP_STATE_EVENT, 0, 0, 0, 0, NEVA_DEADLINE_INFINITE);
    event.handle = silt_cleanup_adopt(&cleanup, event.handle);
    silt_cleanup_ready(mask);
    NevaStatus status = event.status;
    if (status == NEVA_STATUS_OK) status = event.handle
        ? sys_event_wait_epoch(event.handle, deadline, epoch) : NEVA_STATUS_IO;
    silt_cleanup_end(&cleanup);
    return status;
}

static NevaStatus terminal_wait(uint32_t tty, uint64_t epoch) {
    return terminal_wait_until(tty, epoch, NEVA_DEADLINE_INFINITE);
}

static uint64_t terminal_now_ns(void) {
    NevaStartupHandleV1 system;
    if (neva_startup_find("system", &system) != NEVA_STATUS_OK) return 0;
    return sys_rpc(system.handle, SYSTEM_RPC_UPTIME, 0, 0) * 1000000ULL;
}

static int terminal_is_foreground(uint32_t tty) {
    NevaServiceResult result = sys_service_call(tty, PTY_SLAVE_RPC_GET_FOREGROUND,
        0, 0, 0, 0, NEVA_DEADLINE_INFINITE);
    return (int64_t)result.value > 0 && (pid_t)result.value == getpgrp();
}

int silt_tty_read(uint32_t tty, void* buffer, size_t size, int flags) {
    uint8_t* bytes = buffer;
    size_t count = 0;
    uint64_t epoch = sys_signal_epoch();
    NevaServiceResult attributes = sys_service_call(tty, PTY_SLAVE_RPC_GET_ATTRIBUTES,
        0, 0, 0, 0, NEVA_DEADLINE_INFINITE);
    if ((int64_t)attributes.value < 0) return terminal_error(attributes.status);
    int canonical = (attributes.value & NEVA_TTY_FLAG_CANONICAL) != 0;
    NevaServiceResult timing = sys_service_call(tty, PTY_SLAVE_RPC_GET_ATTRIBUTES,
        2, 0, 0, 0, NEVA_DEADLINE_INFINITE);
    if ((int64_t)timing.value < 0) return terminal_error(timing.status);
    uint32_t minimum = (uint8_t)timing.value;
    uint64_t timeout = (uint8_t)(timing.value >> 8) * 100000000ULL;
    uint64_t deadline = !canonical && !minimum && timeout
        ? terminal_now_ns() + timeout : NEVA_DEADLINE_INFINITE;
    while (count < size) {
        SiltCleanup cleanup;
        uint32_t group = silt_group_acquire_guarded(&cleanup, 0);
        NevaServiceResult result = sys_service_call(tty, PTY_SLAVE_RPC_READ,
            1, 0, group, 0, NEVA_DEADLINE_INFINITE);
        silt_cleanup_end(&cleanup);
        if ((int64_t)result.value > 0) {
            bytes[count++] = NEVA_TTY_BYTE_DECODE(result.value);
            if (canonical && (result.value & NEVA_TTY_READ_RECORD_END)) break;
            if (!canonical && !(flags & O_NONBLOCK) && minimum && count >= minimum) break;
            if (!canonical && minimum && timeout) deadline = terminal_now_ns() + timeout;
            continue;
        }
        if (result.status == NEVA_STATUS_OK) break;
        if (result.status == NEVA_STATUS_ACCESS_DENIED) {
            if (sys_signal_epoch() != epoch) {
                errno = EINTR;
                return count ? (int)count : -1;
            }
            // A background reader returns here after SIGTTIN is continued.
            // Ignored/blocked SIGTTIN must not turn into a busy retry loop.
            if (terminal_is_foreground(tty)) continue;
            errno = EIO;
            return count ? (int)count : -1;
        }
        if (result.status == NEVA_STATUS_WOULD_BLOCK) {
            if (flags & O_NONBLOCK) { errno = EAGAIN; return count ? (int)count : -1; }
            if (!canonical && !minimum && (!timeout || count)) return (int)count;
            NevaStatus status = terminal_wait_until(tty, epoch, deadline);
            if (status == NEVA_STATUS_TIMED_OUT && !canonical) return (int)count;
            if (status == NEVA_STATUS_OK || status == NEVA_STATUS_PEER_RESTARTED) continue;
            return count ? (int)count : terminal_error(status);
        }
        return count ? (int)count : terminal_error(result.status);
    }
    return (int)count;
}

int silt_tty_write(uint32_t tty, const void* buffer, size_t size, int flags) {
    const uint8_t* bytes = buffer;
    size_t count = 0;
    uint64_t epoch = sys_signal_epoch();
    while (count < size) {
        uint32_t length = size - count > NEVA_TTY_IO_MAX
            ? NEVA_TTY_IO_MAX : (uint32_t)(size - count);
        // Service payloads are bidirectional. A const/RO source must not be
        // used as the reply destination after ttyd has already emitted bytes.
        uint8_t payload[NEVA_TTY_IO_MAX];
        memcpy(payload, bytes + count, length);
        SiltCleanup cleanup;
        uint32_t group = silt_group_acquire_guarded(&cleanup, 0);
        NevaServiceResult result = sys_service_call(tty, PTY_SLAVE_RPC_WRITE,
            (uint64_t)(uintptr_t)payload, 0, group, length, NEVA_DEADLINE_INFINITE);
        silt_cleanup_end(&cleanup);
        if (result.status == NEVA_STATUS_OK) { count += length; continue; }
        // ttyd consumed no bytes when it stopped a background writer. Retry
        // after fg resumes it; interruption of the Event wait stays EINTR.
        if (result.status == NEVA_STATUS_INTERRUPTED && sys_signal_epoch() == epoch) continue;
        if (result.status == NEVA_STATUS_WOULD_BLOCK) {
            if (flags & O_NONBLOCK) { errno = EAGAIN; return count ? (int)count : -1; }
            NevaStatus status = terminal_wait(tty, epoch);
            if (status == NEVA_STATUS_OK || status == NEVA_STATUS_PEER_RESTARTED) continue;
            return count ? (int)count : terminal_error(status);
        }
        return count ? (int)count : terminal_error(result.status);
    }
    return (int)count;
}

NevaStatus silt_tty_set_foreground(uint32_t tty, uint32_t group) {
    NevaProcessGroupInfoV1 info;
    if (!tty || !group || (NevaStatus)(int64_t)sys_rpc(group, PROCESS_GROUP_RPC_QUERY,
            (uint64_t)(uintptr_t)&info, sizeof(info)) != NEVA_STATUS_OK) {
        return NEVA_STATUS_INVALID_ARGUMENT;
    }
    SiltCleanup cleanup;
    uint32_t mask = silt_cleanup_begin(&cleanup);
    (void)silt_cleanup_adopt(&cleanup, sys_handle_dup(group,
        HANDLE_RIGHT_RPC | HANDLE_RIGHT_SIGNAL | HANDLE_RIGHT_INSPECT | HANDLE_RIGHT_TRANSFER));
    silt_cleanup_ready(mask);
    if (!cleanup.handle) { silt_cleanup_end(&cleanup); return NEVA_STATUS_ACCESS_DENIED; }
    // Same attenuated UART/PTY contract as libneva, with unwind-safe ownership.
    NevaStartupHandleV1 catalog;
    NevaServiceResult query = sys_service_call(tty, PTY_SLAVE_RPC_QUERY,
        0, 0, 0, 0, NEVA_DEADLINE_INFINITE);
    NevaServiceResult result;
    if ((int64_t)query.value >= 0 && (query.value & NEVA_TTY_STATE_UART)
        && neva_startup_find("catalog", &catalog) == NEVA_STATUS_OK) {
        result = sys_service_call(catalog.handle, SESSION_CONTROL_RPC_SET_FOREGROUND,
            info.generation, info.session_id, cleanup.handle, 0, NEVA_DEADLINE_INFINITE);
    } else {
        SiltCleanup own_cleanup;
        uint32_t own = silt_group_acquire_guarded(&own_cleanup, 0);
        NevaTtyForegroundClientV1 request = {
            .magic = NEVA_TTY_CONTROL_MAGIC, .version = NEVA_TTY_ABI_VERSION,
            .size = sizeof(request), .caller_group_handle = own, .target_group_handle = cleanup.handle,
            .target_generation = info.generation, .session_id = info.session_id,
        };
        result = own ? sys_service_call_pair(tty, PTY_SLAVE_RPC_SET_FOREGROUND_CLIENT,
            (uintptr_t)&request, 0, own, cleanup.handle, sizeof(request), NEVA_DEADLINE_INFINITE)
            : (NevaServiceResult){ .status = NEVA_STATUS_ACCESS_DENIED };
        silt_cleanup_end(&own_cleanup);
    }
    silt_cleanup_end(&cleanup);
    return result.status;
}

int silt_tty_is_controlling(uint32_t tty) {
    if (!tty) return 0;
    SiltCleanup cleanup;
    uint32_t group = silt_group_acquire_guarded(&cleanup, 0);
    NevaProcessGroupInfoV1 own, foreground;
    NevaStatus status = group ? (NevaStatus)(int64_t)sys_rpc(group, PROCESS_GROUP_RPC_QUERY,
        (uintptr_t)&own, sizeof(own)) : NEVA_STATUS_NOT_FOUND;
    if (status == NEVA_STATUS_OK) {
        status = sys_service_call(tty, PTY_SLAVE_RPC_GET_FOREGROUND_INFO,
            (uintptr_t)&foreground, 0, 0, sizeof(foreground), NEVA_DEADLINE_INFINITE).status;
    }
    silt_cleanup_end(&cleanup);
    return status == NEVA_STATUS_OK && own.session_id == foreground.session_id
        && own.job_control_id == foreground.job_control_id;
}

pid_t tcgetpgrp(int descriptor) {
    uint32_t tty = silt_descriptor_tty(descriptor);
    if (!tty) return -1;
    if (!silt_tty_is_controlling(tty)) { errno = ENOTTY; return -1; }
    NevaServiceResult result = sys_service_call(tty, PTY_SLAVE_RPC_GET_FOREGROUND,
        0, 0, 0, 0, NEVA_DEADLINE_INFINITE);
    if ((int64_t)result.value < 0) return terminal_error(result.status);
    return (pid_t)result.value;
}

pid_t tcgetsid(int descriptor) {
    uint32_t tty = silt_descriptor_tty(descriptor);
    if (!tty) return -1;
    if (!silt_tty_is_controlling(tty)) { errno = ENOTTY; return -1; }
    return getsid(0);
}

int tcsetpgrp(int descriptor, pid_t process_group) {
    uint32_t tty = silt_descriptor_tty(descriptor);
    if (!tty) return -1;
    if (!silt_tty_is_controlling(tty)) { errno = ENOTTY; return -1; }
    if (process_group <= 0) { errno = EINVAL; return -1; }
    SiltCleanup cleanup;
    uint32_t group = silt_group_acquire_guarded(&cleanup, process_group);
    if (!group) { silt_cleanup_end(&cleanup); errno = EPERM; return -1; }
    uint64_t epoch = sys_signal_epoch();
    NevaStatus status;
    do {
        status = silt_tty_set_foreground(tty, group);
    } while (status == NEVA_STATUS_INTERRUPTED && sys_signal_epoch() == epoch);
    silt_cleanup_end(&cleanup);
    if (status == NEVA_STATUS_ACCESS_DENIED) { errno = EPERM; return -1; }
    return status == NEVA_STATUS_OK ? 0 : terminal_error(status);
}

int tcgetattr(int descriptor, struct termios* attributes) {
    uint32_t tty = silt_descriptor_tty(descriptor);
    if (!tty) return -1;
    if (!attributes) { errno = EFAULT; return -1; }
    NevaServiceResult flags = sys_service_call(tty, PTY_SLAVE_RPC_GET_ATTRIBUTES,
        0, 0, 0, 0, NEVA_DEADLINE_INFINITE);
    NevaServiceResult controls = sys_service_call(tty, PTY_SLAVE_RPC_GET_ATTRIBUTES,
        1, 0, 0, 0, NEVA_DEADLINE_INFINITE);
    if ((int64_t)flags.value < 0 || (int64_t)controls.value < 0) return terminal_error(NEVA_STATUS_IO);
    memset(attributes, 0, sizeof(*attributes));
    if (flags.value & NEVA_TTY_FLAG_CANONICAL) attributes->c_lflag |= ICANON;
    if (flags.value & NEVA_TTY_FLAG_ECHO) attributes->c_lflag |= ECHO;
    if (flags.value & NEVA_TTY_FLAG_SIGNALS) attributes->c_lflag |= ISIG;
    if (flags.value & NEVA_TTY_FLAG_TOSTOP) attributes->c_lflag |= TOSTOP;
    if (flags.value & NEVA_TTY_FLAG_NOFLSH) attributes->c_lflag |= NOFLSH;
    if (flags.value & NEVA_TTY_FLAG_ECHO_ERASE) attributes->c_lflag |= ECHOE;
    if (flags.value & NEVA_TTY_FLAG_ECHO_KILL) attributes->c_lflag |= ECHOK;
    if (flags.value & NEVA_TTY_FLAG_ECHO_NEWLINE) attributes->c_lflag |= ECHONL;
    NevaServiceResult input_flags = sys_service_call(tty, PTY_SLAVE_RPC_GET_ATTRIBUTES,
        3, 0, 0, 0, NEVA_DEADLINE_INFINITE);
    if ((int64_t)input_flags.value < 0) return terminal_error(input_flags.status);
    attributes->c_iflag = (tcflag_t)input_flags.value;
    attributes->c_cc[VERASE] = (cc_t)controls.value;
    attributes->c_cc[VKILL] = (cc_t)(controls.value >> 8);
    attributes->c_cc[VEOF] = (cc_t)(controls.value >> 16);
    attributes->c_cc[VINTR] = (cc_t)(controls.value >> 24);
    attributes->c_cc[VSUSP] = (cc_t)(controls.value >> 32);
    attributes->c_cc[VQUIT] = (cc_t)(controls.value >> 40);
    NevaServiceResult timing = sys_service_call(tty, PTY_SLAVE_RPC_GET_ATTRIBUTES,
        2, 0, 0, 0, NEVA_DEADLINE_INFINITE);
    if ((int64_t)timing.value < 0) return terminal_error(timing.status);
    attributes->c_cc[VMIN] = (cc_t)timing.value;
    attributes->c_cc[VTIME] = (cc_t)(timing.value >> 8);
    return 0;
}

int tcsetattr(int descriptor, int action, const struct termios* attributes) {
    uint32_t tty = silt_descriptor_tty(descriptor);
    if (!tty) return -1;
    if (!attributes) { errno = EFAULT; return -1; }
    if (action != TCSANOW && action != TCSADRAIN && action != TCSAFLUSH) { errno = EINVAL; return -1; }
    if ((attributes->c_iflag & ~(ICRNL | INLCR | IGNCR | ISTRIP)) || attributes->c_oflag
        || attributes->c_cflag
        || (attributes->c_lflag & ~(ICANON | ECHO | ISIG | TOSTOP | NOFLSH | ECHOE | ECHOK | ECHONL))) {
        errno = ENOTSUP; return -1;
    }
    if (attributes->c_ispeed || attributes->c_ospeed) { errno = ENOTSUP; return -1; }
    for (size_t index = VQUIT + 1U; index < NCCS; index++) {
        if (attributes->c_cc[index]) { errno = ENOTSUP; return -1; }
    }
    NevaTtyAttributesV2 request = {
        .base = {
            .magic = NEVA_TTY_ATTRIBUTES_MAGIC, .version = 2, .size = sizeof(request),
            .erase = attributes->c_cc[VERASE], .kill = attributes->c_cc[VKILL],
            .eof = attributes->c_cc[VEOF], .intr = attributes->c_cc[VINTR], .susp = attributes->c_cc[VSUSP],
        },
        .minimum = attributes->c_cc[VMIN], .timeout_deciseconds = attributes->c_cc[VTIME],
        .quit = attributes->c_cc[VQUIT],
        .input_flags = (uint8_t)attributes->c_iflag,
    };
    if (attributes->c_lflag & ICANON) request.base.flags |= NEVA_TTY_FLAG_CANONICAL;
    if (attributes->c_lflag & ECHO) request.base.flags |= NEVA_TTY_FLAG_ECHO;
    if (attributes->c_lflag & ISIG) request.base.flags |= NEVA_TTY_FLAG_SIGNALS;
    if (attributes->c_lflag & TOSTOP) request.base.flags |= NEVA_TTY_FLAG_TOSTOP;
    if (attributes->c_lflag & NOFLSH) request.base.flags |= NEVA_TTY_FLAG_NOFLSH;
    if (attributes->c_lflag & ECHOE) request.base.flags |= NEVA_TTY_FLAG_ECHO_ERASE;
    if (attributes->c_lflag & ECHOK) request.base.flags |= NEVA_TTY_FLAG_ECHO_KILL;
    if (attributes->c_lflag & ECHONL) request.base.flags |= NEVA_TTY_FLAG_ECHO_NEWLINE;
    uint64_t epoch = sys_signal_epoch();
    for (;;) {
        SiltCleanup cleanup;
        uint32_t group = silt_group_acquire_guarded(&cleanup, 0);
        if (!group) { silt_cleanup_end(&cleanup); errno = EPERM; return -1; }
        NevaServiceResult result = sys_service_call(tty, PTY_SLAVE_RPC_SET_ATTRIBUTES_V2,
            (uint64_t)(uintptr_t)&request, (uint64_t)action, group, sizeof(request), NEVA_DEADLINE_INFINITE);
        silt_cleanup_end(&cleanup);
        if (result.status == NEVA_STATUS_INTERRUPTED && sys_signal_epoch() == epoch) continue;
        if (result.status == NEVA_STATUS_WOULD_BLOCK) {
            NevaStatus status = terminal_wait(tty, epoch);
            if (status == NEVA_STATUS_OK) continue;
            return terminal_error(status);
        }
        return result.status == NEVA_STATUS_OK ? 0 : terminal_error(result.status);
    }
}

static int terminal_control(int descriptor, uint32_t method, uint32_t argument) {
    uint32_t tty = silt_descriptor_tty(descriptor);
    if (!tty) return -1;
    uint64_t epoch = sys_signal_epoch();
    for (;;) {
        SiltCleanup cleanup;
        uint32_t group = silt_group_acquire_guarded(&cleanup, 0);
        NevaStatus status = sys_service_call(tty, method, argument, 0, group, 0,
            NEVA_DEADLINE_INFINITE).status;
        silt_cleanup_end(&cleanup);
        if (status == NEVA_STATUS_INTERRUPTED && sys_signal_epoch() == epoch) continue;
        if (status == NEVA_STATUS_WOULD_BLOCK) {
            status = terminal_wait(tty, epoch);
            if (status == NEVA_STATUS_OK) continue;
        }
        return status == NEVA_STATUS_OK ? 0 : terminal_error(status);
    }
}

int tcdrain(int descriptor) { return terminal_control(descriptor, PTY_SLAVE_RPC_DRAIN, 0); }
int tcflush(int descriptor, int selector) {
    if (selector < TCIFLUSH || selector > TCIOFLUSH) { errno = EINVAL; return -1; }
    return terminal_control(descriptor, PTY_SLAVE_RPC_FLUSH, (uint32_t)selector + 1U);
}

// The descriptor publisher blocks signals around this acquisition transaction.
// Every temporary capability is closed before the returned remote is published.
uint32_t silt_pty_open(uint64_t identity, int flags, int master) {
    NevaStartupHandleV1 catalog;
    if (neva_startup_find("catalog", &catalog) != NEVA_STATUS_OK) { errno = ENXIO; return 0; }
    SiltCleanup first;
    uint32_t group = silt_group_acquire_guarded(&first, 0);
    NevaServiceResult controller = group ? sys_service_call(catalog.handle,
        SESSION_CONTROL_RPC_DUP_TTY_CONTROLLER, 0, 0, group, 0, NEVA_DEADLINE_INFINITE)
        : (NevaServiceResult){ .status = NEVA_STATUS_NOT_FOUND };
    silt_cleanup_end(&first);
    if (!controller.handle || controller.status != NEVA_STATUS_OK) { errno = ENXIO; return 0; }
    SiltCleanup second;
    group = silt_group_acquire_guarded(&second, 0);
    NevaServiceResult result = group ? sys_service_call(controller.handle,
        master ? TTY_CONTROLLER_RPC_CREATE_PTY : identity ? TTY_CONTROLLER_RPC_OPEN_SLAVE
            : TTY_CONTROLLER_RPC_OPEN_CONTROLLING,
        master ? NEVA_TTY_CREATE_POSIX : identity, master ? 0
            : ((flags & O_NOCTTY) ? NEVA_TTY_OPEN_NOCTTY : 0)
                | ((flags & O_ACCMODE) != O_WRONLY ? NEVA_TTY_OPEN_READ : 0)
                | ((flags & O_ACCMODE) != O_RDONLY ? NEVA_TTY_OPEN_WRITE : 0),
        group, 0, NEVA_DEADLINE_INFINITE) : (NevaServiceResult){ .status = NEVA_STATUS_NOT_FOUND };
    silt_cleanup_end(&second);
    (void)sys_handle_close(controller.handle);
    if (result.status != NEVA_STATUS_OK || !result.handle) {
        errno = result.status == NEVA_STATUS_ACCESS_DENIED ? EACCES
            : result.status == NEVA_STATUS_LIMIT_REACHED ? ENOSPC
            : result.status == NEVA_STATUS_NO_MEMORY ? ENOMEM : identity ? ENOENT : ENXIO;
        return 0;
    }
    return result.handle;
}

int silt_pty_metadata(uint64_t identity, NevaTtyMetadataV1* metadata) {
    SiltCleanup guard;
    uint32_t mask = silt_cleanup_begin(&guard);
    NevaStartupHandleV1 catalog;
    NevaStatus status = neva_startup_find("catalog", &catalog);
    SiltCleanup group_guard;
    uint32_t group = silt_group_acquire_guarded(&group_guard, 0);
    NevaServiceResult controller = status == NEVA_STATUS_OK && group
        ? sys_service_call(catalog.handle, SESSION_CONTROL_RPC_DUP_TTY_CONTROLLER,
            0, 0, group, 0, NEVA_DEADLINE_INFINITE)
        : (NevaServiceResult){ .status = NEVA_STATUS_NOT_FOUND };
    silt_cleanup_end(&group_guard);
    uint32_t handle = silt_cleanup_adopt(&guard, controller.handle);
    // Identity is carried in arg1 for this payload-bearing controller query.
    status = handle ? sys_service_call(handle, TTY_CONTROLLER_RPC_STAT_SLAVE,
        (uintptr_t)metadata, identity, 0, sizeof(*metadata), NEVA_DEADLINE_INFINITE).status
        : controller.status;
    silt_cleanup_end(&guard);
    silt_cleanup_ready(mask);
    if (status == NEVA_STATUS_OK) return 0;
    errno = status == NEVA_STATUS_NO_MEMORY ? ENOMEM : ENOENT;
    return -1;
}

int silt_pty_io(uint32_t master, void* buffer, size_t size, int flags, int writing) {
    uint8_t* bytes = buffer;
    size_t count = 0;
    uint64_t epoch = sys_signal_epoch();
    while (count < size) {
        uint8_t payload[NEVA_TTY_IO_MAX];
        uint32_t length = size - count > sizeof(payload) ? sizeof(payload) : (uint32_t)(size - count);
        if (writing) memcpy(payload, bytes + count, length);
        NevaServiceResult result = sys_service_call(master,
            writing ? PTY_MASTER_RPC_WRITE_INPUT : PTY_MASTER_RPC_READ_OUTPUT,
            writing ? (uintptr_t)payload : 0, 0, 0, writing ? length : 0, NEVA_DEADLINE_INFINITE);
        if (writing && (int64_t)result.value > 0 && result.value <= length) {
            count += (size_t)result.value; continue;
        }
        if (!writing && (int64_t)result.value > 0) {
            bytes[count++] = NEVA_TTY_BYTE_DECODE(result.value); continue;
        }
        if (!writing && result.status == NEVA_STATUS_OK) return (int)count;
        if (result.status != NEVA_STATUS_WOULD_BLOCK) return count ? (int)count : terminal_error(result.status);
        if (count) return (int)count;
        if (flags & O_NONBLOCK) { errno = EAGAIN; return -1; }
        SiltCleanup cleanup;
        uint32_t mask = silt_cleanup_begin(&cleanup);
        NevaServiceResult event = sys_service_call(master, PTY_MASTER_RPC_DUP_STATE_EVENT,
            0, 0, 0, 0, NEVA_DEADLINE_INFINITE);
        event.handle = silt_cleanup_adopt(&cleanup, event.handle);
        silt_cleanup_ready(mask);
        NevaStatus status = event.handle ? sys_event_wait_epoch(event.handle,
            NEVA_DEADLINE_INFINITE, epoch) : NEVA_STATUS_IO;
        silt_cleanup_end(&cleanup);
        if (status != NEVA_STATUS_OK) return terminal_error(status);
    }
    return (int)count;
}

NevaStatus silt_controlling_set_foreground(uint32_t group) {
    SiltCleanup cleanup;
    uint32_t mask = silt_cleanup_begin(&cleanup);
    uint32_t tty = silt_cleanup_adopt(&cleanup, silt_pty_open(0, O_RDWR | O_NOCTTY, 0));
    silt_cleanup_ready(mask);
    NevaStatus status = tty ? silt_tty_set_foreground(tty, group) : NEVA_STATUS_NOT_FOUND;
    silt_cleanup_end(&cleanup);
    return status;
}
