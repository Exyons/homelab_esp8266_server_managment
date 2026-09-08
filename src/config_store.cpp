#include "config_store.h"
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

bool config_store_save() {
    uint8_t blob[CONFIG_BLOB_SIZE];
    config_serialize(g_config, blob);
    for (size_t i = 0; i < CONFIG_BLOB_SIZE; i++) {
        EEPROM.write(i, blob[i]);
    }
    const bool ok = EEPROM.commit();
    if (ok) g_valid = true;
    return ok;
}

void config_store_factory_reset() {
    for (size_t i = 0; i < CONFIG_BLOB_SIZE; i++) {
        EEPROM.write(i, 0x00);
    }
    EEPROM.commit();
    config_set_defaults(g_config);
    g_valid = false;
}

bool config_store_is_valid() { return g_valid; }
