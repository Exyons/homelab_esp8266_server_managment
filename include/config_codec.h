#pragma once
#include <stdint.h>
#include <stddef.h>

#define CONFIG_MAGIC     0xC0FFEE01u
#define CONFIG_VERSION   1
#define CONFIG_BLOB_SIZE 512

struct Config {
    uint32_t magic;
    uint16_t version;
    char     wifi_ssid[33];
    char     wifi_psk[65];
    char     mqtt_host[65];
    uint16_t mqtt_port;
    char     mqtt_user[33];
    char     mqtt_pass[65];
    char     device_id[33];
    char     mdns_host[33];
    char     upd_user[33];
    char     upd_pass[65];
    bool     ap_forced;
    uint32_t crc32;
};

uint32_t crc32_compute(const uint8_t* data, size_t len);

// Zero-fills c, then applies factory defaults.
void config_set_defaults(Config& c);

// Writes exactly CONFIG_BLOB_SIZE bytes. Stamps magic, version and CRC.
void config_serialize(const Config& in, uint8_t* buf);

// Returns false unless magic and CRC both validate. out is zeroed on failure.
bool config_deserialize(const uint8_t* buf, Config& out);

// True when enough is set for an STA connection attempt.
bool config_is_provisioned(const Config& c);
