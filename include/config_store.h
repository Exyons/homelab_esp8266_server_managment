#pragma once
#include "config_codec.h"

// Reads EEPROM into the in-RAM config. On invalid/blank EEPROM the config is
// reset to defaults and left unprovisioned. Call once from setup().
void config_store_begin();

// The live in-RAM config. Mutate, then call config_store_save().
Config& config();

// Serialises the live config to EEPROM. Returns EEPROM.commit()'s result.
bool config_store_save();

// Zeroes the EEPROM blob and resets the live config to defaults.
void config_store_factory_reset();

// True when the config loaded from EEPROM passed magic+CRC validation.
bool config_store_is_valid();
