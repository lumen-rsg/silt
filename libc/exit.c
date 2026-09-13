#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

// ISO C requires space for at least 32 registrations. _exit deliberately
// bypasses them; fork inherits the registrations through the private image.
static void (*g_callbacks[32])(void);
static unsigned g_callback_count;

int atexit(void (*callback)(void)) {
    if (g_callback_count == 32) { errno = ENOMEM; return -1; }
    g_callbacks[g_callback_count++] = callback;
    return 0;
}

void exit(int status) {
    while (g_callback_count) g_callbacks[--g_callback_count]();
    _exit(status);
}
