#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include_next <stdlib.h>
#pragma GCC diagnostic pop

int posix_openpt(int flags);
int grantpt(int descriptor);
int unlockpt(int descriptor);
char* ptsname(int descriptor);
