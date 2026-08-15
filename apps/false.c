#include "runtime.h"

static volatile uint32_t g_silt_false_status;

void main(void) {
    g_silt_false_status = 1;
    sys_exit(g_silt_false_status);
}
