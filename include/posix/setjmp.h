#pragma once

#include <stdint.h>

// x19-x30, SP, d8-d15, cleanup checkpoint. Rebuild all Silt static images
// together: older implementations did not initialize the final word.
typedef uint64_t jmp_buf[22];
int setjmp(jmp_buf environment) __attribute__((returns_twice));
void longjmp(jmp_buf environment, int value) __attribute__((noreturn));
