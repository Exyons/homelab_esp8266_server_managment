#pragma once
#include "config_codec.h"

// Reads EEPROM into the in-RAM config. On invalid/blank EEPROM the config is
// reset to defaults and left unprovisioned. Call once from setup().
void config_store_begin();

// The live in-RAM config. Mutate, then call config_store_save().
Config& config();

// Serialises the live config to EEPROM. Returns EEPROM.commit()'s result.
bool config_store_save();

// Serialises the supplied config to EEPROM and, only on a successful commit,
// adopts it as the live config. Leaves the live config untouched on failure.
bool config_store_save_from(const Config& candidate);

// Zeroes the EEPROM blob and resets the live config to defaults.
void config_store_factory_reset();

// True when the config loaded from EEPROM passed magic+CRC validation.
bool config_store_is_valid();
