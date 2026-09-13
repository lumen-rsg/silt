#include "libneva.h"
#include "silt_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/termios.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#define SILT_ENVIRONMENT_MAX 64U

static int g_errno;
static char* g_environment[SILT_ENVIRONMENT_MAX + 1U];
// The exec handoff page is DONTFORK; environment pointers must outlive it.
static char g_environment_strings[SILT_EXEC_STRING_BYTES];
char** environ = g_environment;
static mode_t g_umask = 022;

void silt_environment_exec_restore(const SiltExecInfoV3* info) {
    for (size_t index = 0; index <= SILT_ENVIRONMENT_MAX; index++) {
        g_environment[index] = NULL;
    }
    if (!info || info->environment_count > SILT_ENVIRONMENT_MAX
        || info->string_bytes > SILT_EXEC_STRING_BYTES) {
        return;
    }
    for (uint16_t index = 0; index < info->environment_count; index++) {
        uint16_t offset = info->environment[index].offset;
        uint16_t length = info->environment[index].length;
        if (offset > info->string_bytes || length >= info->string_bytes - offset
            || info->strings[offset + length] != '\0') {
            return;
        }
    }
    memcpy(g_environment_strings, info->strings, info->string_bytes);
    for (uint16_t index = 0; index < info->environment_count; index++) {
        g_environment[index] = &g_environment_strings[info->environment[index].offset];
    }
}

int* __errno(void) {
    return &g_errno;
}

void _exit(int status) {
    silt_descriptors_process_exit();
    sys_exit(status);
}

pid_t getpid(void) {
    return (pid_t)sys_getpid();
}

uid_t getuid(void) {
    return (uid_t)neva_getuid();
}

uid_t geteuid(void) {
    return (uid_t)neva_geteuid();
}

gid_t getgid(void) {
    return (gid_t)neva_getgid();
}

gid_t getegid(void) {
    return (gid_t)neva_getegid();
}

int getgroups(int capacity, gid_t groups[]) {
    if (capacity < 0 || (capacity > 0 && !groups)) {
        errno = EINVAL;
        return -1;
    }
    uint16_t compact[16];
    int count = neva_getgroups(compact, capacity > 16 ? 16 : capacity);
    if (count < 0) {
        errno = EINVAL;
        return -1;
    }
    for (int index = 0; index < count; index++) groups[index] = compact[index];
    return count;
}

char* getenv(const char* name) {
    if (!name || !*name || strchr(name, '=')) return NULL;
    size_t length = strlen(name);
    for (size_t index = 0; g_environment[index]; index++) {
        if (strncmp(g_environment[index], name, length) == 0
            && g_environment[index][length] == '=') {
            return g_environment[index] + length + 1U;
        }
    }
    return NULL;
}

int putenv(char* assignment) {
    if (!assignment || !strchr(assignment, '=')) {
        errno = EINVAL;
        return -1;
    }
    size_t name_length = (size_t)(strchr(assignment, '=') - assignment);
    size_t empty = SILT_ENVIRONMENT_MAX;
    for (size_t index = 0; index < SILT_ENVIRONMENT_MAX; index++) {
        if (!g_environment[index]) {
            empty = index;
            break;
        }
        if (strncmp(g_environment[index], assignment, name_length) == 0
            && g_environment[index][name_length] == '=') {
            g_environment[index] = assignment;
            return 0;
        }
    }
    if (empty == SILT_ENVIRONMENT_MAX) {
        errno = ENOMEM;
        return -1;
    }
    g_environment[empty] = assignment;
    g_environment[empty + 1U] = NULL;
    return 0;
}

char* setlocale(int category, const char* locale) {
    static char c_locale[] = "C";
    (void)category;
    if (!locale || locale[0] == '\0' || strcmp(locale, "C") == 0
        || strcmp(locale, "POSIX") == 0) {
        return c_locale;
    }
    return NULL;
}

sighandler_t signal(int signal_number, sighandler_t handler) {
    sighandler_t previous = sys_signal_action(signal_number, handler, 0, 1, NULL);
    if (previous == SIG_ERR) errno = EINVAL;
    return previous;
}

int sigaction(int signal_number, const struct sigaction* action,
              struct sigaction* previous) {
    if (signal_number <= 0 || signal_number >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    if (action && action->sa_flags != 0) {
        errno = ENOTSUP;
        return -1;
    }
    uint32_t old_mask = 0;
    sighandler_t prior = sys_signal_action(signal_number,
        action ? action->sa_handler : SIG_DFL,
        action ? (uint32_t)action->sa_mask : 0, action != NULL, &old_mask);
    if (prior == SIG_ERR) {
        errno = EINVAL;
        return -1;
    }
    if (previous) {
        previous->sa_handler = prior;
        previous->sa_mask = old_mask;
        previous->sa_flags = 0;
    }
    return 0;
}

int sigemptyset(sigset_t* set) {
    if (!set) {
        errno = EINVAL;
        return -1;
    }
    *set = 0;
    return 0;
}

int sigfillset(sigset_t* set) {
    if (!set) {
        errno = EINVAL;
        return -1;
    }
    *set = (sigset_t)((1ULL << NSIG) - 1ULL);
    return 0;
}

int sigaddset(sigset_t* set, int signal_number) {
    if (!set || signal_number <= 0 || signal_number >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    *set |= (sigset_t)(1UL << signal_number);
    return 0;
}

int sigdelset(sigset_t* set, int signal_number) {
    if (!set || signal_number <= 0 || signal_number >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    *set &= (sigset_t)~(1UL << signal_number);
    return 0;
}

int sigismember(const sigset_t* set, int signal_number) {
    if (!set || signal_number <= 0 || signal_number >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    return (*set & (sigset_t)(1UL << signal_number)) != 0;
}

int sigprocmask(int operation, const sigset_t* set, sigset_t* previous) {
    if (set && operation != SIG_SETMASK && operation != SIG_BLOCK
        && operation != SIG_UNBLOCK) {
        errno = EINVAL;
        return -1;
    }
    uint64_t old = sys_signal_mask(set ? (uint32_t)operation : 3,
        set ? (uint32_t)*set : 0);
    if (old == UINT64_MAX) { errno = EINVAL; return -1; }
    if (previous) *previous = (sigset_t)old;
    return 0;
}

int sigsuspend(const sigset_t* mask) {
    if (!mask) { errno = EFAULT; return -1; }
    NevaStatus result = sys_signal_suspend((uint32_t)*mask);
    errno = result == NEVA_STATUS_INTERRUPTED ? EINTR : EINVAL;
    return -1;
}

int raise(int signal_number) {
    return kill(getpid(), signal_number);
}

char* strerror(int error) {
    switch (error) {
        case 0: return "success";
        case EACCES: return "permission denied";
        case EBADF: return "bad file descriptor";
        case EFAULT: return "bad address";
        case EINVAL: return "invalid argument";
        case EIO: return "I/O error";
        case ENOENT: return "not found";
        case EMFILE: return "Too many open files";
        case ENFILE: return "Too many open files in system";
        case ENOMEM: return "out of memory";
        case ENOSYS: return "not implemented";
        case ERANGE: return "out of range";
        case ESRCH: return "no such process";
        default: return "unknown error";
    }
}

char* strsignal(int signal_number) {
    switch (signal_number) {
        case SIGILL: return "Illegal instruction";
        case SIGBUS: return "Bus error";
        case SIGSEGV: return "Segmentation fault";
        case SIGINT: return "Interrupt";
        case SIGKILL: return "Killed";
        case SIGTERM: return "Terminated";
        case SIGSTOP: return "Stopped";
        case SIGTSTP: return "Stopped";
        case SIGTTIN: return "Stopped (tty input)";
        case SIGTTOU: return "Stopped (tty output)";
        default: return "Signal";
    }
}

uint16_t silt_creation_mask(void) { return (uint16_t)g_umask; }

mode_t umask(mode_t mask) {
    mode_t previous = g_umask;
    g_umask = mask & 0777;
    return previous;
}

size_t mbrlen(const char* string, size_t size, mbstate_t* state) {
    return mbrtowc(NULL, string, size, state);
}

size_t mbrtowc(wchar_t* output, const char* string, size_t size,
               mbstate_t* state) {
    (void)state;
    if (!string) return 0;
    if (size == 0) return (size_t)-2;
    unsigned char byte = (unsigned char)string[0];
    if (byte > 0x7fU) {
        errno = EILSEQ;
        return (size_t)-1;
    }
    if (output) *output = byte;
    return byte == 0 ? 0 : 1;
}

size_t mbsrtowcs(wchar_t* destination, const char** source, size_t capacity,
                 mbstate_t* state) {
    (void)state;
    if (!source || !*source) return 0;
    size_t length = 0;
    while ((*source)[length]) {
        if ((unsigned char)(*source)[length] > 0x7fU) {
            errno = EILSEQ;
            return (size_t)-1;
        }
        if (destination && length < capacity) destination[length] = (*source)[length];
        length++;
        if (destination && length == capacity) return length;
    }
    if (destination) {
        destination[length] = 0;
        *source = NULL;
    }
    return length;
}

wchar_t* wcschr(const wchar_t* string, wchar_t character) {
    do {
        if (*string == character) return (wchar_t*)string;
    } while (*string++ != 0);
    return NULL;
}

wctype_t wctype(const char* property) {
    if (strcmp(property, "blank") == 0) return 1;
    if (strcmp(property, "space") == 0) return 2;
    return 0;
}

int iswctype(wint_t character, wctype_t property) {
    if (property == 1) return character == ' ' || character == '\t';
    if (property == 2) return character == ' ' || (character >= '\t' && character <= '\r');
    return 0;
}

int iswblank(wint_t character) {
    return character == ' ' || character == '\t';
}

int iswspace(wint_t character) {
    return character == ' ' || (character >= '\t' && character <= '\r');
}

int getpagesize(void) { return 4096; }
