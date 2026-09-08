#include <unity.h>
#include <string.h>
#include "config_codec.h"

void setUp(void) {}
void tearDown(void) {}

void test_struct_layout_is_440_bytes(void) {
    TEST_ASSERT_EQUAL_UINT32(440, (uint32_t)sizeof(Config));
}

void test_crc32_known_vector(void) {
    const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32_compute(data, sizeof(data)));
}

void test_defaults_are_not_provisioned(void) {
    Config c;
    config_set_defaults(c);
    TEST_ASSERT_FALSE(config_is_provisioned(c));
    TEST_ASSERT_EQUAL_UINT16(8883, c.mqtt_port);
    TEST_ASSERT_EQUAL_STRING("admin", c.upd_user);
    TEST_ASSERT_EQUAL_STRING("admin", c.upd_pass);
}

void test_roundtrip_preserves_fields(void) {
    Config in;
    config_set_defaults(in);
    strcpy(in.wifi_ssid, "MyNetwork");
    strcpy(in.wifi_psk,  "hunter2hunter2");
    strcpy(in.mqtt_host, "broker.example.com");
    in.mqtt_port = 8884;
    in.ap_forced = true;

    uint8_t blob[CONFIG_BLOB_SIZE];
    config_serialize(in, blob);

    Config out;
    TEST_ASSERT_TRUE(config_deserialize(blob, out));
    TEST_ASSERT_EQUAL_STRING("MyNetwork", out.wifi_ssid);
    TEST_ASSERT_EQUAL_STRING("hunter2hunter2", out.wifi_psk);
    TEST_ASSERT_EQUAL_STRING("broker.example.com", out.mqtt_host);
    TEST_ASSERT_EQUAL_UINT16(8884, out.mqtt_port);
    TEST_ASSERT_TRUE(out.ap_forced);
    TEST_ASSERT_TRUE(config_is_provisioned(out));
}

void test_blank_blob_is_rejected(void) {
    uint8_t blob[CONFIG_BLOB_SIZE];
    memset(blob, 0x00, sizeof(blob));
    Config out;
    TEST_ASSERT_FALSE(config_deserialize(blob, out));
}

void test_erased_flash_blob_is_rejected(void) {
    uint8_t blob[CONFIG_BLOB_SIZE];
    memset(blob, 0xFF, sizeof(blob));
    Config out;
    TEST_ASSERT_FALSE(config_deserialize(blob, out));
}

void test_corrupt_crc_is_rejected(void) {
    Config in;
    config_set_defaults(in);
    strcpy(in.wifi_ssid, "MyNetwork");
    uint8_t blob[CONFIG_BLOB_SIZE];
    config_serialize(in, blob);
    blob[8] ^= 0xFF;                 // flip a byte inside wifi_ssid
    Config out;
    TEST_ASSERT_FALSE(config_deserialize(blob, out));
}

void test_wrong_magic_is_rejected(void) {
    Config in;
    config_set_defaults(in);
    uint8_t blob[CONFIG_BLOB_SIZE];
    config_serialize(in, blob);
    blob[0] ^= 0xFF;                 // corrupt magic
    Config out;
    TEST_ASSERT_FALSE(config_deserialize(blob, out));
}

void test_serialize_ignores_caller_padding(void) {
    Config clean, dirty;

    // Initialize clean with config_set_defaults
    config_set_defaults(clean);
    strcpy(clean.wifi_ssid, "TestNet");
    clean.mqtt_port = 8884;

    // Initialize dirty with same field values but dirty padding
    memset(&dirty, 0xAB, sizeof(dirty));  // Fill everything with 0xAB
    dirty.magic = CONFIG_MAGIC;
    dirty.version = CONFIG_VERSION;
    dirty.mqtt_port = 8884;
    dirty.ap_forced = false;
    memset(dirty.wifi_ssid, 0, sizeof(dirty.wifi_ssid));
    strcpy(dirty.wifi_ssid, "TestNet");
    memset(dirty.wifi_psk, 0, sizeof(dirty.wifi_psk));
    memset(dirty.mqtt_host, 0, sizeof(dirty.mqtt_host));
    memset(dirty.mqtt_user, 0, sizeof(dirty.mqtt_user));
    memset(dirty.mqtt_pass, 0, sizeof(dirty.mqtt_pass));
    memset(dirty.device_id, 0, sizeof(dirty.device_id));
    memset(dirty.mdns_host, 0, sizeof(dirty.mdns_host));
    strcpy(dirty.mdns_host, "esp-updater");
    memset(dirty.upd_user, 0, sizeof(dirty.upd_user));
    strcpy(dirty.upd_user, "admin");
    memset(dirty.upd_pass, 0, sizeof(dirty.upd_pass));
    strcpy(dirty.upd_pass, "admin");

    uint8_t blob_clean[CONFIG_BLOB_SIZE], blob_dirty[CONFIG_BLOB_SIZE];
    config_serialize(clean, blob_clean);
    config_serialize(dirty, blob_dirty);
    TEST_ASSERT_EQUAL_INT(0, memcmp(blob_clean, blob_dirty, CONFIG_BLOB_SIZE));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_struct_layout_is_440_bytes);
    RUN_TEST(test_crc32_known_vector);
    RUN_TEST(test_defaults_are_not_provisioned);
    RUN_TEST(test_roundtrip_preserves_fields);
    RUN_TEST(test_blank_blob_is_rejected);
    RUN_TEST(test_erased_flash_blob_is_rejected);
    RUN_TEST(test_corrupt_crc_is_rejected);
    RUN_TEST(test_wrong_magic_is_rejected);
    RUN_TEST(test_serialize_ignores_caller_padding);
    return UNITY_END();
}
