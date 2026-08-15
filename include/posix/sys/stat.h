#pragma once

#include_next <sys/stat.h>

int lstat(const char* path, struct stat* status);
