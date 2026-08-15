#include "libneva.h"

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
char** environ = g_environment;
static sigset_t g_signal_mask;
static mode_t g_umask = 022;

int* __errno(void) {
    return &g_errno;
}

void _exit(int status) {
    sys_exit(status);
}

int write(int descriptor, const void* buffer, size_t size) {
    if ((descriptor != STDOUT_FILENO && descriptor != STDERR_FILENO)
        || (!buffer && size != 0)) {
        errno = descriptor == STDOUT_FILENO || descriptor == STDERR_FILENO
            ? EFAULT : EBADF;
        return -1;
    }
    const unsigned char* bytes = buffer;
    for (size_t index = 0; index < size; index++) neva_putc((char)bytes[index]);
    return (int)size;
}

int read(int descriptor, void* buffer, size_t size) {
    if (descriptor != STDIN_FILENO || (!buffer && size != 0)) {
        errno = descriptor == STDIN_FILENO ? EFAULT : EBADF;
        return -1;
    }
    unsigned char* bytes = buffer;
    for (size_t index = 0; index < size; index++) bytes[index] = (unsigned char)neva_getc();
    return (int)size;
}

int close(int descriptor) {
    if (descriptor >= STDIN_FILENO && descriptor <= STDERR_FILENO) return 0;
    errno = EBADF;
    return -1;
}

int isatty(int descriptor) {
    if (descriptor >= STDIN_FILENO && descriptor <= STDERR_FILENO) return 1;
    errno = EBADF;
    return 0;
}

pid_t getpid(void) {
    return (pid_t)sys_getpid();
}

pid_t getppid(void) {
    return 0;
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
    sighandler_t previous = sys_sigaction(signal_number, handler);
    if (previous == SIG_ERR) errno = EINVAL;
    return previous;
}

int sigaction(int signal_number, const struct sigaction* action,
              struct sigaction* previous) {
    if (signal_number <= 0 || signal_number >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    sighandler_t prior = sys_sigaction(
        signal_number, action ? action->sa_handler : SIG_DFL);
    if (prior == SIG_ERR) {
        errno = EINVAL;
        return -1;
    }
    if (previous) {
        previous->sa_handler = prior;
        previous->sa_mask = 0;
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
    if (previous) *previous = g_signal_mask;
    if (!set) return 0;
    if (operation == SIG_SETMASK) g_signal_mask = *set;
    else if (operation == SIG_BLOCK) g_signal_mask |= *set;
    else if (operation == SIG_UNBLOCK) g_signal_mask &= ~*set;
    else {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int sigsuspend(const sigset_t* mask) {
    if (mask) g_signal_mask = *mask;
    errno = EINTR;
    return -1;
}

int kill(pid_t process, int signal_number) {
    if (process <= 0) {
        errno = ENOSYS;
        return -1;
    }
    int status = sys_kill((uint32_t)process, signal_number);
    if (status < 0) errno = ESRCH;
    return status;
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
        case ENOMEM: return "out of memory";
        case ENOSYS: return "not implemented";
        case ERANGE: return "out of range";
        case ESRCH: return "no such process";
        default: return "unknown error";
    }
}

char* strsignal(int signal_number) {
    switch (signal_number) {
        case SIGINT: return "Interrupt";
        case SIGKILL: return "Killed";
        case SIGTERM: return "Terminated";
        case SIGSTOP: return "Stopped";
        case SIGTSTP: return "Stopped (tty)";
        default: return "Signal";
    }
}

char* getcwd(char* buffer, size_t size) {
    if (!buffer || size < 2) {
        errno = ERANGE;
        return NULL;
    }
    buffer[0] = '/';
    buffer[1] = '\0';
    return buffer;
}

int chdir(const char* path) {
    if (path && strcmp(path, "/") == 0) return 0;
    errno = ENOENT;
    return -1;
}

mode_t umask(mode_t mask) {
    mode_t previous = g_umask;
    g_umask = mask & 0777;
    return previous;
}

int stat(const char* path, struct stat* status) {
    (void)path;
    if (status) memset(status, 0, sizeof(*status));
    errno = ENOENT;
    return -1;
}

int lstat(const char* path, struct stat* status) {
    return stat(path, status);
}

int fstat(int descriptor, struct stat* status) {
    if (status) memset(status, 0, sizeof(*status));
    if (descriptor >= STDIN_FILENO && descriptor <= STDERR_FILENO) {
        if (status) status->st_mode = S_IFCHR;
        return 0;
    }
    errno = EBADF;
    return -1;
}

int open(const char* path, int flags, ...) {
    (void)path;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

off_t lseek(int descriptor, off_t offset, int origin) {
    (void)descriptor;
    (void)offset;
    (void)origin;
    errno = ESPIPE;
    return (off_t)-1;
}

int fcntl(int descriptor, int command, ...) {
    (void)descriptor;
    (void)command;
    errno = ENOSYS;
    return -1;
}

int dup(int descriptor) {
    (void)descriptor;
    errno = ENOSYS;
    return -1;
}

int dup2(int source, int destination) {
    (void)source;
    (void)destination;
    errno = ENOSYS;
    return -1;
}

int pipe(int descriptors[2]) {
    (void)descriptors;
    errno = ENOSYS;
    return -1;
}

pid_t fork(void) {
    errno = ENOSYS;
    return -1;
}

pid_t vfork(void) {
    errno = ENOSYS;
    return -1;
}

int execve(const char* path, char* const arguments[], char* const environment[]) {
    (void)path;
    (void)arguments;
    (void)environment;
    errno = ENOSYS;
    return -1;
}

pid_t wait3(int* status, int options, struct rusage* usage) {
    (void)status;
    (void)options;
    (void)usage;
    errno = ECHILD;
    return -1;
}

DIR* opendir(const char* path) {
    (void)path;
    errno = ENOSYS;
    return NULL;
}

struct dirent* readdir(DIR* directory) {
    (void)directory;
    errno = ENOSYS;
    return NULL;
}

int closedir(DIR* directory) {
    (void)directory;
    errno = ENOSYS;
    return -1;
}

int tcgetattr(int descriptor, struct termios* attributes) {
    if (!attributes || !isatty(descriptor)) {
        if (!attributes) errno = EINVAL;
        return -1;
    }
    memset(attributes, 0, sizeof(*attributes));
    attributes->c_lflag = ICANON;
    return 0;
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
