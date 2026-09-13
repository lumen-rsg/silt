// Exercise the production conversions at the boundaries used by GNU counts.
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define strtoll silt_strtoll
#define strtoull silt_strtoull
#define strtoimax silt_strtoimax
#define strtoumax silt_strtoumax
#include "../libc/integer.c"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
int main(void) {
    char* end;
    errno = EDOM;
    CHECK(strtoll(" -42x", &end, 10) == -42 && *end == 'x' && errno == EDOM);
    CHECK(strtoll("-9223372036854775808", &end, 10) == LLONG_MIN && !*end);
    CHECK(strtoll("9223372036854775807", &end, 10) == LLONG_MAX && !*end);
    errno = 0;
    CHECK(strtoll("-9223372036854775809!", &end, 10) == LLONG_MIN);
    CHECK(errno == ERANGE && *end == '!');
    errno = 0;
    CHECK(strtoll("9223372036854775808", &end, 10) == LLONG_MAX && errno == ERANGE);
    errno = 0;
    CHECK(strtoull("18446744073709551615x", &end, 10) == ULLONG_MAX && *end == 'x' && !errno);
    CHECK(strtoull("-1", &end, 10) == ULLONG_MAX && !*end && !errno);
    CHECK(strtoull("-18446744073709551616x", &end, 10) == ULLONG_MAX);
    CHECK(errno == ERANGE && *end == 'x');
    const char* invalid[] = {"", "  ", "+", "-", "--1", "+-1", "-+1", "- 1", "+\t1", "word"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        errno = EDOM;
        CHECK(strtoll(invalid[i], &end, 10) == 0 && end == invalid[i] && errno == EDOM);
    }
    CHECK(strtoll("-0x7f!", &end, 0) == -127 && *end == '!');
    CHECK(strtoull("077", &end, 0) == 63 && !*end);
    CHECK(strtoull("0x", &end, 0) == 0 && *end == 'x');
    CHECK(strtoull("z", &end, 36) == 35 && !*end);
    errno = 0;
    CHECK(strtoull("1", &end, 37) == 0 && errno == EINVAL && *end == '1');
    CHECK(strtoimax("-123", NULL, 10) == -123 && strtoumax("123", NULL, 10) == 123);
    return 0;
}
