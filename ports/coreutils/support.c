#include "silt-system.h"
#include "xalloc.h"
#include "exitfail.h"

const char* program_name;
const char* Version = "9.11";

void set_program_name(const char* name) {
    const char* slash = strrchr(name, '/');
    program_name = slash ? slash + 1 : name;
}

#undef error
static void report_error(int number, const char* format, va_list arguments) {
    fprintf(stderr, "%s: ", program_name);
    vfprintf(stderr, format, arguments);
    if (number) fprintf(stderr, ": %s", strerror(number));
    fputc('\n', stderr);
}

void error(int status, int number, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    report_error(number, format, arguments);
    va_end(arguments);
    if (status) exit(status);
}

void silt_error_exit(int status, int number, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    report_error(number, format, arguments);
    va_end(arguments);
    exit(status);
}

void close_stdout(void) {
    int failed = ferror(stdout);
    int saved_errno = errno;
    if (fclose(stdout) != 0) { failed = 1; saved_errno = errno; }
    if (failed) {
        error(0, saved_errno, "write error");
        _exit(exit_failure);
    }
    if (ferror(stderr) || fclose(stderr) != 0) _exit(exit_failure);
}

void emit_try_help(void) {
    fprintf(stderr, "Try '%s --help' for more information.\n", program_name);
}

void emit_mandatory_arg_note(void) {
    fputs("\nMandatory arguments to long options are mandatory for short options too.\n", stdout);
}

void emit_ancillary_info(const char* name) {
    fprintf(stdout, "\nGNU coreutils: https://www.gnu.org/software/coreutils/\n"
            "Silt C2a port: %s; C locale.\n", name);
}

void version_etc_va(FILE* stream, const char* name, const char* package,
                    const char* version, va_list authors) {
    fprintf(stream, "%s (%s) %s\nCopyright (C) 2026 Free Software Foundation, Inc.\n"
            "License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n"
            "This is free software: you are free to change and redistribute it.\n"
            "There is NO WARRANTY, to the extent permitted by law.\n\nWritten by ", name, package, version);
    const char* author;
    const char* separator = "";
    while ((author = va_arg(authors, const char*))) {
        fprintf(stream, "%s%s", separator, author);
        separator = ", ";
    }
    fputs(".\n", stream);
}

void version_etc(FILE* stream, const char* name, const char* package, const char* version, ...) {
    va_list authors;
    va_start(authors, version);
    version_etc_va(stream, name, package, version, authors);
    va_end(authors);
}

void xalloc_die(void) { error(exit_failure, 0, "memory exhausted"); abort(); }
void* ximalloc(idx_t size) {
    if (size < 0) xalloc_die();
    void* result = malloc(size ? (size_t)size : 1U);
    if (!result) xalloc_die();
    return result;
}

const char* quote(const char* text) {
    // Port diagnostics use deterministic ASCII escaping, independent of locale.
    static char* buffer;
    free(buffer);
    size_t length = strlen(text);
    if (length > (SIZE_MAX - 3U) / 4U) xalloc_die();
    buffer = malloc(length * 4U + 3U);
    if (!buffer) xalloc_die();
    char* out = buffer;
    *out++ = '\'';
    const char* hex = "0123456789abcdef";
    for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
        if (*p < 32 || *p >= 127 || *p == '\'' || *p == '\\') {
            *out++ = '\\'; *out++ = 'x'; *out++ = hex[*p >> 4]; *out++ = hex[*p & 15];
        } else *out++ = (char)*p;
    }
    *out++ = '\'';
    *out = 0;
    return buffer;
}
