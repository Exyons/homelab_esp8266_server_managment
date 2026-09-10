#include <unity.h>
#include "image_detect.h"

void setUp(void) {}
void tearDown(void) {}

void test_e9_first_byte_is_firmware(void) {
    const uint8_t buf[] = {0xE9, 0x02, 0x00, 0x00};
    TEST_ASSERT_EQUAL_INT(IMAGE_FIRMWARE, image_detect(buf, sizeof(buf)));
}

void test_littlefs_image_is_filesystem(void) {
    const uint8_t buf[] = {0x10, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_INT(IMAGE_FILESYSTEM, image_detect(buf, sizeof(buf)));
}

void test_empty_buffer_is_filesystem(void) {
    TEST_ASSERT_EQUAL_INT(IMAGE_FILESYSTEM, image_detect(nullptr, 0));
}

void test_detection_ignores_filename_semantics(void) {
    // A firmware image stays firmware regardless of what it is called.
    const uint8_t buf[] = {0xE9};
    TEST_ASSERT_EQUAL_INT(IMAGE_FIRMWARE, image_detect(buf, 1));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_e9_first_byte_is_firmware);
    RUN_TEST(test_littlefs_image_is_filesystem);
    RUN_TEST(test_empty_buffer_is_filesystem);
    RUN_TEST(test_detection_ignores_filename_semantics);
    return UNITY_END();
}
