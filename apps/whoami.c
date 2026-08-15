#include "runtime.h"

void main(void) {
    char username[32];
    uint16_t uid = neva_geteuid();
    if (silt_username(uid, username, sizeof(username)) < 0) {
        int_to_str_buf(uid, username, sizeof(username));
    }
    neva_println(username);
    sys_exit(0);
}
