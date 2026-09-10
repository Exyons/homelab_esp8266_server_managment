#include "config_codec.h"
#include <string.h>

static_assert(sizeof(Config) == 440, "Config layout changed; bump CONFIG_VERSION");

uint32_t crc32_compute(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

// Field-by-field copy. A struct assignment would carry the source's padding
// bytes across, making the CRC depend on uninitialised memory.
static void copy_fields(Config& dst, const Config& src) {
    dst.magic     = src.magic;
    dst.version   = src.version;
    dst.mqtt_port = src.mqtt_port;
    dst.ap_forced = src.ap_forced;
    dst.crc32     = src.crc32;
    memcpy(dst.wifi_ssid, src.wifi_ssid, sizeof(dst.wifi_ssid));
    memcpy(dst.wifi_psk,  src.wifi_psk,  sizeof(dst.wifi_psk));
    memcpy(dst.mqtt_host, src.mqtt_host, sizeof(dst.mqtt_host));
    memcpy(dst.mqtt_user, src.mqtt_user, sizeof(dst.mqtt_user));
    memcpy(dst.mqtt_pass, src.mqtt_pass, sizeof(dst.mqtt_pass));
    memcpy(dst.device_id, src.device_id, sizeof(dst.device_id));
    memcpy(dst.mdns_host, src.mdns_host, sizeof(dst.mdns_host));
    memcpy(dst.upd_user,  src.upd_user,  sizeof(dst.upd_user));
    memcpy(dst.upd_pass,  src.upd_pass,  sizeof(dst.upd_pass));
}

void config_set_defaults(Config& c) {
    memset(&c, 0, sizeof(c));
    c.magic     = CONFIG_MAGIC;
    c.version   = CONFIG_VERSION;
    c.mqtt_port = 8883;
    c.ap_forced = false;
    strncpy(c.upd_user, "admin", sizeof(c.upd_user) - 1);
    strncpy(c.upd_pass, "admin", sizeof(c.upd_pass) - 1);
    strncpy(c.mdns_host, "esp-updater", sizeof(c.mdns_host) - 1);
}

void config_serialize(const Config& in, uint8_t* buf) {
    Config tmp;
    memset(&tmp, 0, sizeof(tmp));
    copy_fields(tmp, in);
    tmp.magic   = CONFIG_MAGIC;
    tmp.version = CONFIG_VERSION;
    tmp.crc32   = 0;
    tmp.crc32   = crc32_compute(reinterpret_cast<const uint8_t*>(&tmp),
                                offsetof(Config, crc32));
    memset(buf, 0, CONFIG_BLOB_SIZE);
    memcpy(buf, &tmp, sizeof(Config));
}

bool config_deserialize(const uint8_t* buf, Config& out) {
    Config tmp;
    memset(&tmp, 0, sizeof(tmp));
    memcpy(&tmp, buf, sizeof(Config));

    if (tmp.magic != CONFIG_MAGIC) {
        memset(&out, 0, sizeof(out));
        return false;
    }
    const uint32_t stored = tmp.crc32;
    tmp.crc32 = 0;
    const uint32_t actual = crc32_compute(reinterpret_cast<const uint8_t*>(&tmp),
                                          offsetof(Config, crc32));
    if (stored != actual) {
        memset(&out, 0, sizeof(out));
        return false;
    }
    tmp.crc32 = stored;
    memset(&out, 0, sizeof(out));
    copy_fields(out, tmp);
    return true;
}

bool config_is_provisioned(const Config& c) {
    return c.magic == CONFIG_MAGIC && c.wifi_ssid[0] != '\0';
}
