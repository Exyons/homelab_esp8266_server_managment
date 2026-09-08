#include <unity.h>
#include "net_decide.h"

void setUp(void) {}
void tearDown(void) {}

void test_unprovisioned_goes_to_ap(void) {
    TEST_ASSERT_EQUAL_INT(NET_AP, net_initial_state(false, false, false));
}

void test_provisioned_goes_to_sta(void) {
    TEST_ASSERT_EQUAL_INT(NET_STA_CONNECTING, net_initial_state(true, false, false));
}

void test_ap_forced_overrides_provisioned(void) {
    TEST_ASSERT_EQUAL_INT(NET_AP, net_initial_state(true, true, false));
}

void test_reset_request_overrides_everything(void) {
    TEST_ASSERT_EQUAL_INT(NET_AP, net_initial_state(true, false, true));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_unprovisioned_goes_to_ap);
    RUN_TEST(test_provisioned_goes_to_sta);
    RUN_TEST(test_ap_forced_overrides_provisioned);
    RUN_TEST(test_reset_request_overrides_everything);
    return UNITY_END();
}
