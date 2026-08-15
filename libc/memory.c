#include "libneva.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct SiltHeapBlock {
    size_t size;
    int free;
} SiltHeapBlock;

#define SILT_HEAP_ALIGNMENT 16U
#define SILT_HEAP_HEADER sizeof(SiltHeapBlock)

static SiltHeapBlock* g_heap_start;
static SiltHeapBlock* g_heap_end;

static size_t aligned_size(size_t size) {
    if (size > SIZE_MAX - (SILT_HEAP_ALIGNMENT - 1U)) return 0;
    return (size + SILT_HEAP_ALIGNMENT - 1U) & ~(SILT_HEAP_ALIGNMENT - 1U);
}

static SiltHeapBlock* next_block(SiltHeapBlock* block) {
    uintptr_t next = (uintptr_t)block + SILT_HEAP_HEADER + block->size;
    return next < (uintptr_t)g_heap_end ? (SiltHeapBlock*)next : NULL;
}

void* malloc(size_t requested) {
    size_t size = aligned_size(requested);
    if (size == 0) return NULL;
    if (!g_heap_start) {
        g_heap_start = (SiltHeapBlock*)sys_sbrk(0);
        g_heap_end = g_heap_start;
    }

    for (SiltHeapBlock* block = g_heap_start;
         block && block < g_heap_end; block = next_block(block)) {
        if (!block->free || block->size < size) continue;
        if (block->size >= size + SILT_HEAP_HEADER + SILT_HEAP_ALIGNMENT) {
            SiltHeapBlock* remainder = (SiltHeapBlock*)(
                (uintptr_t)block + SILT_HEAP_HEADER + size);
            remainder->size = block->size - size - SILT_HEAP_HEADER;
            remainder->free = 1;
            block->size = size;
        }
        block->free = 0;
        return (void*)((uintptr_t)block + SILT_HEAP_HEADER);
    }

    size_t total = SILT_HEAP_HEADER + size;
    if (total < USER_PAGE_SIZE) total = USER_PAGE_SIZE;
    if (total > SIZE_MAX - (USER_PAGE_SIZE - 1U)) return NULL;
    total = (total + USER_PAGE_SIZE - 1U) & ~(size_t)(USER_PAGE_SIZE - 1U);
    if (sys_sbrk((intptr_t)total) == (void*)-1) return NULL;

    SiltHeapBlock* block = g_heap_end;
    block->size = total - SILT_HEAP_HEADER;
    block->free = 0;
    g_heap_end = (SiltHeapBlock*)((uintptr_t)block + total);
    if (block->size >= size + SILT_HEAP_HEADER + SILT_HEAP_ALIGNMENT) {
        SiltHeapBlock* remainder = (SiltHeapBlock*)(
            (uintptr_t)block + SILT_HEAP_HEADER + size);
        remainder->size = block->size - size - SILT_HEAP_HEADER;
        remainder->free = 1;
        block->size = size;
    }
    return (void*)((uintptr_t)block + SILT_HEAP_HEADER);
}

void free(void* pointer) {
    if (!pointer) return;
    SiltHeapBlock* block = (SiltHeapBlock*)(
        (uintptr_t)pointer - SILT_HEAP_HEADER);
    block->free = 1;
    SiltHeapBlock* following = next_block(block);
    while (following && following->free) {
        block->size += SILT_HEAP_HEADER + following->size;
        following = next_block(block);
    }
}

void* realloc(void* pointer, size_t requested) {
    if (!pointer) return malloc(requested);
    if (requested == 0) {
        free(pointer);
        return NULL;
    }
    size_t size = aligned_size(requested);
    if (size == 0) return NULL;
    SiltHeapBlock* block = (SiltHeapBlock*)(
        (uintptr_t)pointer - SILT_HEAP_HEADER);
    if (block->size >= size) return pointer;
    void* replacement = malloc(size);
    if (!replacement) return NULL;
    memcpy(replacement, pointer, block->size);
    free(pointer);
    return replacement;
}

char* strchr(const char* string, int character) {
    char wanted = (char)character;
    do {
        if (*string == wanted) return (char*)string;
    } while (*string++ != '\0');
    return NULL;
}

char* strcpy(char* destination, const char* source) {
    char* result = destination;
    while ((*destination++ = *source++) != '\0') {}
    return result;
}

char* strdup(const char* string) {
    size_t size = strlen(string) + 1U;
    char* copy = malloc(size);
    if (copy) memcpy(copy, string, size);
    return copy;
}

size_t strspn(const char* string, const char* accepted) {
    size_t length = 0;
    while (string[length] && strchr(accepted, string[length])) length++;
    return length;
}

size_t strcspn(const char* string, const char* rejected) {
    size_t length = 0;
    while (string[length] && !strchr(rejected, string[length])) length++;
    return length;
}

char* strpbrk(const char* string, const char* accepted) {
    while (*string) {
        if (strchr(accepted, *string)) return (char*)string;
        string++;
    }
    return NULL;
}

char* strstr(const char* haystack, const char* needle) {
    size_t length = strlen(needle);
    if (length == 0) return (char*)haystack;
    for (; *haystack; haystack++) {
        if (strncmp(haystack, needle, length) == 0) return (char*)haystack;
    }
    return NULL;
}

void* memrchr(const void* memory, int character, size_t size) {
    const unsigned char* bytes = memory;
    while (size > 0) {
        size--;
        if (bytes[size] == (unsigned char)character) return (void*)(bytes + size);
    }
    return NULL;
}

char* strcat(char* destination, const char* source) {
    char* end = destination + strlen(destination);
    strcpy(end, source);
    return destination;
}

char* strrchr(const char* string, int character) {
    const char* match = NULL;
    do {
        if (*string == (char)character) match = string;
    } while (*string++ != '\0');
    return (char*)match;
}

char* stpncpy(char* destination, const char* source, size_t size) {
    while (size > 0 && *source) {
        *destination++ = *source++;
        size--;
    }
    char* end = destination;
    while (size-- > 0) *destination++ = '\0';
    return end;
}

int strcasecmp(const char* left, const char* right) {
    while (*left && *right) {
        unsigned char a = (unsigned char)*left++;
        unsigned char b = (unsigned char)*right++;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
        if (a != b) return (int)a - (int)b;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

int strcoll(const char* left, const char* right) {
    return strcmp(left, right);
}

char* strtok(char* string, const char* separators) {
    static char* next;
    if (string) next = string;
    if (!next) return NULL;
    next += strspn(next, separators);
    if (*next == '\0') {
        next = NULL;
        return NULL;
    }
    char* token = next;
    next += strcspn(next, separators);
    if (*next) *next++ = '\0';
    else next = NULL;
    return token;
}

static int digit_value(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'z') return character - 'a' + 10;
    if (character >= 'A' && character <= 'Z') return character - 'A' + 10;
    return -1;
}

unsigned long long strtoull(const char* string, char** end, int base) {
    const char* cursor = string;
    while (*cursor == ' ' || (*cursor >= '\t' && *cursor <= '\r')) cursor++;
    int negative = *cursor == '-';
    if (*cursor == '+' || *cursor == '-') cursor++;
    if ((base == 0 || base == 16) && cursor[0] == '0'
        && (cursor[1] == 'x' || cursor[1] == 'X')) {
        base = 16;
        cursor += 2;
    } else if (base == 0) {
        base = cursor[0] == '0' ? 8 : 10;
    }
    if (base < 2 || base > 36) {
        errno = EINVAL;
        if (end) *end = (char*)string;
        return 0;
    }
    unsigned long long value = 0;
    const char* first = cursor;
    for (int digit; (digit = digit_value(*cursor)) >= 0 && digit < base; cursor++) {
        unsigned long long limit = ULLONG_MAX / (unsigned)base;
        if (value > limit
            || (value == limit
                && (unsigned)digit > ULLONG_MAX % (unsigned)base)) {
            value = ULLONG_MAX;
            errno = ERANGE;
            while ((digit = digit_value(cursor[1])) >= 0 && digit < base) cursor++;
            cursor++;
            break;
        }
        value = value * (unsigned)base + (unsigned)digit;
    }
    if (end) *end = (char*)(cursor == first ? string : cursor);
    return negative ? 0ULL - value : value;
}

long long strtoll(const char* string, char** end, int base) {
    const char* cursor = string;
    while (*cursor == ' ' || (*cursor >= '\t' && *cursor <= '\r')) cursor++;
    int negative = *cursor == '-';
    unsigned long long magnitude = strtoull(string, end, base);
    if (errno == ERANGE) return negative ? LLONG_MIN : LLONG_MAX;
    if (negative) {
        if (magnitude > (unsigned long long)LLONG_MAX + 1ULL) {
            errno = ERANGE;
            return LLONG_MIN;
        }
        return magnitude == (unsigned long long)LLONG_MAX + 1ULL
            ? LLONG_MIN : -(long long)magnitude;
    }
    if (magnitude > LLONG_MAX) {
        errno = ERANGE;
        return LLONG_MAX;
    }
    return (long long)magnitude;
}

int atoi(const char* string) {
    return (int)strtoll(string, NULL, 10);
}

double strtod(const char* string, char** end) {
    const char* cursor = string;
    while (*cursor == ' ' || (*cursor >= '\t' && *cursor <= '\r')) cursor++;
    int negative = *cursor == '-';
    if (*cursor == '+' || *cursor == '-') cursor++;
    double value = 0.0;
    int digits = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        value = value * 10.0 + (double)(*cursor++ - '0');
        digits++;
    }
    if (*cursor == '.') {
        cursor++;
        double place = 0.1;
        while (*cursor >= '0' && *cursor <= '9') {
            value += (double)(*cursor++ - '0') * place;
            place *= 0.1;
            digits++;
        }
    }
    if (end) *end = (char*)(digits ? cursor : string);
    return negative ? -value : value;
}

void qsort(void* base, size_t count, size_t width,
           int (*compare)(const void*, const void*)) {
    unsigned char* bytes = base;
    if (!bytes || width == 0 || count < 2) return;
    for (size_t outer = 1; outer < count; outer++) {
        size_t inner = outer;
        while (inner > 0
               && compare(bytes + (inner - 1U) * width,
                          bytes + inner * width) > 0) {
            for (size_t offset = 0; offset < width; offset++) {
                unsigned char temporary = bytes[(inner - 1U) * width + offset];
                bytes[(inner - 1U) * width + offset] = bytes[inner * width + offset];
                bytes[inner * width + offset] = temporary;
            }
            inner--;
        }
    }
}

void abort(void) {
    sys_exit(134);
}
