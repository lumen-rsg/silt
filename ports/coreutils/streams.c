#include "silt-streams.h"
#undef read

void emit_stdin_note(void) {
    fputs("\nWith no FILE, or when FILE is -, read standard input.\n", stdout);
}
void emit_size_note(void) {
    fputs("\nNUM may have a multiplier suffix: b=512, kB=1000, K=1024, M=1024*1024, and so on.\n", stdout);
}
void write_error(void) { error(EXIT_FAILURE, errno, "write error"); abort(); }
void* xmalloc(size_t size) {
    void* memory = malloc(size ? size : 1);
    if (!memory) xalloc_die();
    return memory;
}
void* xnmalloc(size_t count, size_t size) {
    size_t total;
    if (ckd_mul(&total, count, size)) xalloc_die();
    return xmalloc(total);
}
void* xinmalloc(idx_t count, idx_t size) {
    if (count < 0 || size < 0) xalloc_die();
    return xnmalloc((size_t)count, (size_t)size);
}
void* xrealloc(void* pointer, size_t size) {
    void* memory = realloc(pointer, size ? size : 1);
    if (!memory) xalloc_die();
    return memory;
}
void* xizalloc(idx_t size) {
    void* memory = ximalloc(size);
    memset(memory, 0, (size_t)size);
    return memory;
}
void* xpalloc(void* memory, idx_t* capacity, idx_t minimum, ptrdiff_t maximum, idx_t size) {
    idx_t next;
    if (minimum < 0 || size <= 0 || ckd_add(&next, *capacity, minimum)) xalloc_die();
    if (maximum >= 0 && next > maximum) xalloc_die();
    idx_t bytes;
    if (ckd_mul(&bytes, next, size)) xalloc_die();
    memory = xrealloc(memory, (size_t)bytes);
    *capacity = next;
    return memory;
}
void* xalignalloc(idx_t alignment, idx_t size) {
    if (alignment < (idx_t)sizeof(void*) || (alignment & (alignment - 1)) || size < 0) xalloc_die();
    size_t total;
    if (ckd_add(&total, (size_t)size, (size_t)alignment - 1U)
        || ckd_add(&total, total, sizeof(void*))) xalloc_die();
    void* allocation = xmalloc(total);
    uintptr_t address = ((uintptr_t)allocation + sizeof(void*) + alignment - 1U)
        & ~((uintptr_t)alignment - 1U);
    ((void**)address)[-1] = allocation;
    return (void*)address;
}
void alignfree(void* pointer) { if (pointer) free(((void**)pointer)[-1]); }
void* rawmemchr(const void* buffer, int byte) {
    const unsigned char* p = buffer;
    while (*p != (unsigned char)byte) p++;
    return (void*)p;
}

int fpurge(FILE* stream) { (void)stream; return 0; }

void silt_unsupported(const char* feature) { error(EXIT_FAILURE, ENOTSUP, "%s", feature); abort(); }
int silt_argmatch(const char* option, const char* argument, const char* const* values) {
    int match = -1;
    size_t length = strlen(argument);
    for (int i = 0; values[i]; i++) {
        if (!strcmp(argument, values[i])) return i;
        if (!strncmp(argument, values[i], length)) {
            if (match != -1) error(EXIT_FAILURE, 0, "%s: ambiguous argument %s", option, quote(argument));
            match = i;
        }
    }
    if (match < 0) error(EXIT_FAILURE, 0, "%s: invalid argument %s", option, quote(argument));
    return match;
}
int posix2_version(void) {
    long long version = 200809;
    const char* text = getenv("_POSIX2_VERSION");
    if (text && *text) {
        char* end;
        long long parsed = strtoll(text, &end, 10);
        if (!*end) version = parsed;
    }
    return version < INT_MIN ? INT_MIN : version > INT_MAX ? INT_MAX : (int)version;
}
char* umaxtostr(uintmax_t value, char* buffer) {
    char* end = buffer + INT_BUFSIZE_BOUND(uintmax_t) - 1;
    *end = 0;
    do { *--end = '0' + value % 10; value /= 10; } while (value);
    return end;
}
char* imaxtostr(intmax_t value, char* buffer) {
    char* out = umaxtostr(value < 0 ? 0U - (uintmax_t)value : (uintmax_t)value, buffer);
    if (value < 0) *--out = '-';
    return out;
}
