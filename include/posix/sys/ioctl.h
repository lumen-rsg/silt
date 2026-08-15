#pragma once

#include <stdint.h>

struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

#define TIOCGWINSZ 0x5413UL
#define TIOCSWINSZ 0x5414UL

int ioctl(int descriptor, unsigned long request, ...);
