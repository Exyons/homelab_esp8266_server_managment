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

void test_serialize_zeroes_padding_deterministically(void) {
    Config a, b;
    config_set_defaults(a);
    memset(&b, 0xAB, sizeof(b));     // fill b with junk including padding
    config_set_defaults(b);          // must fully zero it again
    strcpy(a.wifi_ssid, "Net");
    strcpy(b.wifi_ssid, "Net");

    uint8_t blob_a[CONFIG_BLOB_SIZE], blob_b[CONFIG_BLOB_SIZE];
    config_serialize(a, blob_a);
    config_serialize(b, blob_b);
    TEST_ASSERT_EQUAL_INT(0, memcmp(blob_a, blob_b, CONFIG_BLOB_SIZE));
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
    RUN_TEST(test_serialize_zeroes_padding_deterministically);
    return UNITY_END();
}
