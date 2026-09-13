#include "../libc/path-normalize.h"
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
int main(void) {
    struct { const char* cwd; const char* path; const char* expected; } cases[] = {
        {"/", "/boot/c1/../c1//", "/boot/c1"},
        {"/", "/boot/c1/..", "/boot"},
        {"/", "/a/b/../../c", "/c"},
        {"/", "/../.././a", "/a"},
        {"/boot/c1", "../d5/./../c1", "/boot/c1"},
        {"/boot/c1", "..", "/boot"},
        {"/", "..", "/"},
        {"/boot", ".", "/boot"},
        {"/", "///", "/"},
        {"/", "/a/.hidden/..x/...", "/a/.hidden/..x/..."},
    };
    char output[NEVA_FS_PATH_MAX + 1U];
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        CHECK(silt_normalize_path(cases[i].cwd, cases[i].path, output) == 0);
        CHECK(!strcmp(output, cases[i].expected));
    }
    CHECK(silt_normalize_path("/", "", output) == -1 && errno == ENOENT);
    CHECK(silt_normalize_path("/", NULL, output) == -1 && errno == ENOENT);
    char large[NEVA_FS_PATH_MAX + 2U];
    memset(large, 'a', sizeof(large) - 1U); large[sizeof(large) - 1U] = 0;
    CHECK(silt_normalize_path("/", large, output) == -1 && errno == ENAMETOOLONG);
    large[NEVA_FS_COMPONENT_MAX + 1U] = 0;
    CHECK(silt_normalize_path("/", large, output) == -1 && errno == ENAMETOOLONG);
    large[NEVA_FS_COMPONENT_MAX] = 0;
    CHECK(silt_normalize_path("/", large, output) == 0);
    CHECK(strlen(output) == NEVA_FS_COMPONENT_MAX + 1U);
    return 0;
}
