#pragma once
#include "net_decide.h"

// Reads the FLASH button, decides the boot state, brings up AP or STA.
void net_begin();

// Drives the state machine. Call every loop().
void net_loop();

NetState    net_state();
bool        net_is_ap();
const char* net_ap_ssid();

// Persists ap_forced then reboots into the requested mode.
void net_set_ap_forced(bool forced);

// Wipes EEPROM then reboots into AP.
void net_factory_reset_and_reboot();
