#include <unity.h>
#include "net_util.h"

void setUp(void) {}
void tearDown(void) {}

// Addresses are little-endian packed, matching ESP8266 IPAddress storage:
// 10.10.10.42 -> 0x2A0A0A0A
static uint32_t pack(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24);
}

void test_slash24_broadcast(void) {
    TEST_ASSERT_EQUAL_HEX32(pack(10,10,10,255),
        broadcast_addr(pack(10,10,10,42), pack(255,255,255,0)));
}

void test_slash16_broadcast(void) {
    TEST_ASSERT_EQUAL_HEX32(pack(172,16,255,255),
        broadcast_addr(pack(172,16,3,9), pack(255,255,0,0)));
}

void test_slash8_broadcast(void) {
    TEST_ASSERT_EQUAL_HEX32(pack(10,255,255,255),
        broadcast_addr(pack(10,1,2,3), pack(255,0,0,0)));
}

void test_slash25_broadcast(void) {
    TEST_ASSERT_EQUAL_HEX32(pack(192,168,1,127),
        broadcast_addr(pack(192,168,1,10), pack(255,255,255,128)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_slash24_broadcast);
    RUN_TEST(test_slash16_broadcast);
    RUN_TEST(test_slash8_broadcast);
    RUN_TEST(test_slash25_broadcast);
    return UNITY_END();
}
