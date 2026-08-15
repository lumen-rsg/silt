#include "runtime.h"

void main(void) {
    char username[32];
    uint16_t uid = neva_getuid();
    if (silt_username(uid, username, sizeof(username)) < 0) {
        int_to_str_buf(uid, username, sizeof(username));
    }
    neva_print("uid=");
    neva_print_int(uid);
    neva_print("(");
    neva_print(username);
    neva_print(") gid=");
    neva_print_int(neva_getgid());
    neva_print(" euid=");
    neva_print_int(neva_geteuid());
    neva_println("");
    sys_exit(0);
}
