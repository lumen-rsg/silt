#include "libneva.h"
#include "silt_internal.h"

#include <errno.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static int terminal_error(NevaStatus status) {
    errno = status == NEVA_STATUS_INTERRUPTED ? EINTR
        : status == NEVA_STATUS_ACCESS_DENIED ? EACCES : EIO;
    return -1;
}

static NevaStatus terminal_wait(uint32_t tty, uint64_t epoch) {
    SiltCleanup cleanup;
    uint32_t mask = silt_cleanup_begin(&cleanup);
    NevaServiceResult event = sys_service_call(tty,
        PTY_SLAVE_RPC_DUP_STATE_EVENT, 0, 0, 0, 0, NEVA_DEADLINE_INFINITE);
    event.handle = silt_cleanup_adopt(&cleanup, event.handle);
    silt_cleanup_ready(mask);
    NevaStatus status = event.status;
    if (status == NEVA_STATUS_OK) status = event.handle
        ? sys_event_wait_epoch(event.handle, NEVA_DEADLINE_INFINITE, epoch) : NEVA_STATUS_IO;
    silt_cleanup_end(&cleanup);
    return status;
}

static int terminal_is_foreground(uint32_t tty) {
    NevaServiceResult result = sys_service_call(tty, PTY_SLAVE_RPC_GET_FOREGROUND,
        0, 0, 0, 0, NEVA_DEADLINE_INFINITE);
    return (int64_t)result.value > 0 && (pid_t)result.value == getpgrp();
}

int silt_tty_read(uint32_t tty, void* buffer, size_t size) {
    uint8_t* bytes = buffer;
    size_t count = 0;
    uint64_t epoch = sys_signal_epoch();
    NevaServiceResult attributes = sys_service_call(tty, PTY_SLAVE_RPC_GET_ATTRIBUTES,
        0, 0, 0, 0, NEVA_DEADLINE_INFINITE);
    if ((int64_t)attributes.value < 0) return terminal_error(attributes.status);
    int canonical = (attributes.value & NEVA_TTY_FLAG_CANONICAL) != 0;
    while (count < size) {
        SiltCleanup cleanup;
        uint32_t group = silt_group_acquire_guarded(&cleanup, 0);
        NevaServiceResult result = sys_service_call(tty, PTY_SLAVE_RPC_READ,
            0, 0, group, 0, NEVA_DEADLINE_INFINITE);
        silt_cleanup_end(&cleanup);
        if ((int64_t)result.value > 0) {
            bytes[count++] = NEVA_TTY_BYTE_DECODE(result.value);
            if (!canonical || bytes[count - 1] == '\n') break;
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
            NevaStatus status = terminal_wait(tty, epoch);
            if (status == NEVA_STATUS_OK || status == NEVA_STATUS_PEER_RESTARTED) continue;
            return count ? (int)count : terminal_error(status);
        }
        return count ? (int)count : terminal_error(result.status);
    }
    return (int)count;
}

int silt_tty_write(uint32_t tty, const void* buffer, size_t size) {
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
        NevaTtyForegroundV1 request = {
            .magic = NEVA_TTY_CONTROL_MAGIC, .version = NEVA_TTY_ABI_VERSION,
            .size = sizeof(request), .process_group_handle = cleanup.handle,
            .expected_group_generation = info.generation, .session_id = info.session_id,
        };
        result = sys_service_call(tty, PTY_SLAVE_RPC_SET_FOREGROUND,
            (uint64_t)(uintptr_t)&request, 0, cleanup.handle, sizeof(request), NEVA_DEADLINE_INFINITE);
    }
    silt_cleanup_end(&cleanup);
    return result.status;
}

pid_t tcgetpgrp(int descriptor) {
    uint32_t tty = silt_descriptor_tty(descriptor);
    if (!tty) return -1;
    NevaServiceResult result = sys_service_call(tty, PTY_SLAVE_RPC_GET_FOREGROUND,
        0, 0, 0, 0, NEVA_DEADLINE_INFINITE);
    if ((int64_t)result.value < 0) return terminal_error(result.status);
    return (pid_t)result.value;
}

int tcsetpgrp(int descriptor, pid_t process_group) {
    uint32_t tty = silt_descriptor_tty(descriptor);
    if (!tty) return -1;
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
    attributes->c_cc[VERASE] = (cc_t)controls.value;
    attributes->c_cc[VKILL] = (cc_t)(controls.value >> 8);
    attributes->c_cc[VEOF] = (cc_t)(controls.value >> 16);
    attributes->c_cc[VINTR] = (cc_t)(controls.value >> 24);
    attributes->c_cc[VSUSP] = (cc_t)(controls.value >> 32);
    return 0;
}

int tcsetattr(int descriptor, int action, const struct termios* attributes) {
    uint32_t tty = silt_descriptor_tty(descriptor);
    if (!tty) return -1;
    if (!attributes) { errno = EFAULT; return -1; }
    if (action != TCSANOW || attributes->c_iflag || attributes->c_oflag
        || attributes->c_cflag || (attributes->c_lflag & ~(ICANON | ECHO | ISIG | TOSTOP))) {
        errno = ENOTSUP; return -1;
    }
    NevaTtyAttributesV1 request = {
        .magic = NEVA_TTY_ATTRIBUTES_MAGIC, .version = NEVA_TTY_ABI_VERSION,
        .size = sizeof(request), .erase = attributes->c_cc[VERASE],
        .kill = attributes->c_cc[VKILL], .eof = attributes->c_cc[VEOF],
        .intr = attributes->c_cc[VINTR], .susp = attributes->c_cc[VSUSP],
    };
    if (attributes->c_lflag & ICANON) request.flags |= NEVA_TTY_FLAG_CANONICAL;
    if (attributes->c_lflag & ECHO) request.flags |= NEVA_TTY_FLAG_ECHO;
    if (attributes->c_lflag & ISIG) request.flags |= NEVA_TTY_FLAG_SIGNALS;
    if (attributes->c_lflag & TOSTOP) request.flags |= NEVA_TTY_FLAG_TOSTOP;
    uint64_t epoch = sys_signal_epoch();
    for (;;) {
        SiltCleanup cleanup;
        uint32_t group = silt_group_acquire_guarded(&cleanup, 0);
        if (!group) { silt_cleanup_end(&cleanup); errno = EPERM; return -1; }
        NevaServiceResult result = sys_service_call(tty, PTY_SLAVE_RPC_SET_ATTRIBUTES,
            (uint64_t)(uintptr_t)&request, 0, group, sizeof(request), NEVA_DEADLINE_INFINITE);
        silt_cleanup_end(&cleanup);
        if (result.status == NEVA_STATUS_INTERRUPTED && sys_signal_epoch() == epoch) continue;
        return result.status == NEVA_STATUS_OK ? 0 : terminal_error(result.status);
    }
}
