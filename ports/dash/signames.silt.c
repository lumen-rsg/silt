#include <signal.h>

// Indexed by the frozen Neva signal number. The final sentinel is retained for
// dash's generated-table convention.
const char* const signal_names[NSIG + 1] = {
    "EXIT",
    "HUP",
    "INT",
    "QUIT",
    "4",
    "5",
    "ABRT",
    "BUS",
    "FPE",
    "KILL",
    "10",
    "SEGV",
    "SYS",
    "PIPE",
    "ALRM",
    "TERM",
    "USR1",
    "USR2",
    "CHLD",
    "CONT",
    "STOP",
    "TSTP",
    "TTIN",
    "TTOU",
    0,
};
