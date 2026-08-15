#include "runtime.h"

int main(int argc, char* argv[]) {
    int all = argc > 1 && strcmp(argv[1], "-a") == 0;
    neva_print("Silt");
    if (all) neva_print(" neva 0.1.0 aarch64");
    neva_println("");
    return 0;
}
