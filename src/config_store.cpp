#include "config_store.h"
#include "log_store.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>

static Config g_config;
static bool   g_valid = false;

void config_store_begin() {
    EEPROM.begin(CONFIG_BLOB_SIZE);
    uint8_t blob[CONFIG_BLOB_SIZE];
    for (size_t i = 0; i < CONFIG_BLOB_SIZE; i++) {
        blob[i] = EEPROM.read(i);
    }
    g_valid = config_deserialize(blob, g_config);
    if (!g_valid) {
        config_set_defaults(g_config);
    }
}

Config& config() { return g_config; }

bool config_store_save_from(const Config& candidate) {
    uint8_t blob[CONFIG_BLOB_SIZE];
    config_serialize(candidate, blob);
    for (size_t i = 0; i < CONFIG_BLOB_SIZE; i++) {
        EEPROM.write(i, blob[i]);
    }
    if (!EEPROM.commit()) return false;   // live config left untouched
    if (&candidate != &g_config) g_config = candidate;
    g_valid = true;
    return true;
}

bool config_store_save() { return config_store_save_from(g_config); }

void config_store_factory_reset() {
    for (size_t i = 0; i < CONFIG_BLOB_SIZE; i++) {
        EEPROM.write(i, 0x00);
    }
    // Nothing downstream can act on a failure here — the caller reboots — but
    // an unlogged failed erase would look exactly like a successful one.
    if (!EEPROM.commit()) {
        log_error("Could not erase saved settings; they may survive the reset");
    }
    config_set_defaults(g_config);
    g_valid = false;
}

bool config_store_is_valid() { return g_valid; }
