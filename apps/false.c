#include "runtime.h"

static volatile uint32_t g_silt_false_status;

int main(void) {
    g_silt_false_status = 1;
    return g_silt_false_status;
}
