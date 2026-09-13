#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

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
        && (cursor[1] == 'x' || cursor[1] == 'X')
        && digit_value(cursor[2]) >= 0 && digit_value(cursor[2]) < 16) {
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
    int overflow = 0;
    const char* first = cursor;
    for (int digit; (digit = digit_value(*cursor)) >= 0 && digit < base; cursor++) {
        unsigned long long limit = ULLONG_MAX / (unsigned)base;
        if (value > limit
            || (value == limit
                && (unsigned)digit > ULLONG_MAX % (unsigned)base)) {
            value = ULLONG_MAX;
            overflow = 1;
            errno = ERANGE;
            while ((digit = digit_value(cursor[1])) >= 0 && digit < base) cursor++;
            cursor++;
            break;
        }
        value = value * (unsigned)base + (unsigned)digit;
    }
    if (end) *end = (char*)(cursor == first ? string : cursor);
    return negative && !overflow ? 0ULL - value : value;
}

long long strtoll(const char* string, char** end, int base) {
    const char* cursor = string;
    while (*cursor == ' ' || (*cursor >= '\t' && *cursor <= '\r')) cursor++;
    int negative = *cursor == '-';
    if (*cursor == '-' || *cursor == '+') cursor++;
    if (*cursor == '-' || *cursor == '+' || *cursor == ' '
        || (*cursor >= '\t' && *cursor <= '\r')) {
        if (end) *end = (char*)string;
        return 0;
    }
    int saved_errno = errno;
    errno = 0;
    char* limit;
    unsigned long long magnitude = strtoull(cursor, &limit, base);
    if (end) *end = limit == cursor ? (char*)string : limit;
    unsigned long long maximum = negative ? (unsigned long long)LLONG_MAX + 1U : LLONG_MAX;
    if (errno == ERANGE || magnitude > maximum) {
        errno = ERANGE;
        return negative ? LLONG_MIN : LLONG_MAX;
    }
    if (!errno) errno = saved_errno;
    if (negative) return magnitude == maximum ? LLONG_MIN : -(long long)magnitude;
    return (long long)magnitude;
}

intmax_t strtoimax(const char* string, char** end, int base) { return strtoll(string, end, base); }
uintmax_t strtoumax(const char* string, char** end, int base) { return strtoull(string, end, base); }

