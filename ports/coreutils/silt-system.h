#pragma once
#include <config.h>
#include <errno.h>
#include <getopt.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dirname.h"
#include "idx.h"

#define _(text) (text)
#define N_(text) (text)
#define proper_name(text) (text)
#define streq(a, b) (strcmp(a, b) == 0)
#define FALLTHROUGH __attribute__((fallthrough))
#define affirm(condition) do { if (!(condition)) abort(); } while (0)
#define HELP_OPTION_DESCRIPTION "      --help     display this help and exit\n"
#define VERSION_OPTION_DESCRIPTION "      --version  output version information and exit\n"
#define USAGE_BUILTIN_WARNING "\nYour shell may provide its own version of %s.\n"
#define GETOPT_HELP_OPTION_DECL "help", no_argument, NULL, -130
#define GETOPT_VERSION_OPTION_DECL "version", no_argument, NULL, -131
#define case_GETOPT_HELP_CHAR case -130: usage(EXIT_SUCCESS); break
#define case_GETOPT_VERSION_CHAR(name, ...) \
    case -131: version_etc(stdout, name, PACKAGE_NAME, Version, __VA_ARGS__, (char*)NULL); \
        exit(EXIT_SUCCESS); break
#define oputs(text) fputs(text, stdout)

extern const char* program_name;
extern const char* Version;
static inline void initialize_main(int* argc, char*** argv) { (void)argc; (void)argv; }
static inline void bindtextdomain(const char* domain, const char* path) { (void)domain; (void)path; }
static inline void textdomain(const char* domain) { (void)domain; }
static inline int c_isxdigit(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
void set_program_name(const char* name);
void close_stdout(void);
void emit_try_help(void);
void emit_mandatory_arg_note(void);
void emit_ancillary_info(const char* name);
void version_etc(FILE* stream, const char* name, const char* package, const char* version, ...);
#include "error.h"
