#pragma once

#include <stdint.h>

static inline uint32_t htonl(uint32_t value) {
    return __builtin_bswap32(value);
}

static inline uint32_t ntohl(uint32_t value) {
    return __builtin_bswap32(value);
}
