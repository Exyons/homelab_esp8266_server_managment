#include "image_detect.h"

ImageType image_detect(const uint8_t* bytes, size_t len) {
    if (bytes == nullptr || len < 1) return IMAGE_FILESYSTEM;
    return bytes[0] == 0xE9 ? IMAGE_FIRMWARE : IMAGE_FILESYSTEM;
}
