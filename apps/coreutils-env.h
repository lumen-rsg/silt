#pragma once
#include <unistd.h>

// Identical exec environment for the Linux oracle and the Silt target.
static int coreutils_test_environment(const char* path, char** arguments) {
    char* environment[] = {
        "LC_ALL=C", "ALPHA=one", "EMPTY=", "SPACED=two words",
        "LINES=a\nb", "DUP=first", "DUP=second", "=unnamed", "MALFORMED", NULL,
    };
    execve(path, arguments, environment);
    return 127;
}
