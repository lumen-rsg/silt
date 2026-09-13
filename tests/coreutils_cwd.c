#include <errno.h>
#include <stdlib.h>
#include <string.h>
static int allocations;
static int refuse;
static void* tracked_malloc(size_t size) {
    allocations++;
    return refuse ? NULL : malloc(size);
}
#define malloc tracked_malloc
#include "../libc/cwd-copy.h"
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
int main(void) {
    char buffer[8] = "keep";
    CHECK(!silt_copy_cwd("/abc", buffer, 4) && errno == ERANGE && !strcmp(buffer, "keep"));
    CHECK(!silt_copy_cwd("/abc", buffer, 0) && errno == ERANGE);
    CHECK(!silt_copy_cwd("/abc", NULL, 1) && errno == ERANGE && allocations == 0);
    CHECK(silt_copy_cwd("/abc", buffer, 5) == buffer && !strcmp(buffer, "/abc"));
    char* result = silt_copy_cwd("/abc", NULL, 0);
    CHECK(result && !strcmp(result, "/abc") && allocations == 1);
    free(result);
    result = silt_copy_cwd("/", NULL, 16);
    CHECK(result && !strcmp(result, "/"));
    free(result);
    refuse = 1;
    CHECK(!silt_copy_cwd("/abc", NULL, 0) && errno == ENOMEM);
    return 0;
}
