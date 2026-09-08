#include "net_util.h"

uint32_t broadcast_addr(uint32_t ip, uint32_t netmask) {
    return ip | ~netmask;
}
