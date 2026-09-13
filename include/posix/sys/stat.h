#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include_next <sys/stat.h>
#pragma GCC diagnostic pop

int lstat(const char* path, struct stat* status);
