#include "../apps/coreutils-env.h"
int main(int argc, char** argv) {
    return argc < 3 ? 127 : coreutils_test_environment(argv[1], argv + 2);
}
