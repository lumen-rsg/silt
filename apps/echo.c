#include "runtime.h"

int main(int argc, char* argv[]) {
    for (int index = 1; index < argc; index++) {
        if (index > 1) neva_putc(' ');
        neva_print(argv[index]);
    }
    neva_println("");
    return 0;
}
