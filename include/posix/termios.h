#pragma once

#define ECHO 1U
#define ISIG 4U
#define TOSTOP 8U
#define VERASE 0
#define VKILL 1
#define VEOF 2
#define VINTR 3
#define VSUSP 4

#include <stdint.h>

typedef uint32_t tcflag_t;
typedef uint8_t cc_t;
typedef uint32_t speed_t;

#define NCCS 20

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_cc[NCCS];
    speed_t c_ispeed;
    speed_t c_ospeed;
};

#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define ICANON (1U << 1)

int tcgetattr(int descriptor, struct termios* attributes);
int tcsetattr(int descriptor, int action, const struct termios* attributes);
int tcgetpgrp(int descriptor);
int tcsetpgrp(int descriptor, int process_group);
