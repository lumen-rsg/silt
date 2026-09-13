#pragma once
void error(int status, int number, const char* format, ...);
void silt_error_exit(int status, int number, const char* format, ...) __attribute__((noreturn));
// Preserve the conditional nonreturning contract needed by upstream callers.
#define error(status, ...) ((status) ? silt_error_exit(status, __VA_ARGS__) : error(0, __VA_ARGS__))
