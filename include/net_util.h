#pragma once
#include <stdint.h>

// Directed broadcast for the given address/netmask, in the same byte order as
// the inputs. Callers pass ESP8266 IPAddress values via their uint32_t form.
uint32_t broadcast_addr(uint32_t ip, uint32_t netmask);
