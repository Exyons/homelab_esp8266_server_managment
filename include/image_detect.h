#pragma once
#include <stdint.h>
#include <stddef.h>

enum ImageType { IMAGE_FILESYSTEM = 0, IMAGE_FIRMWARE = 1 };

// ESP8266 firmware images begin with 0xE9. Anything else is treated as a
// filesystem image. Replaces the previous filename-substring heuristic, which
// bricked the device when a firmware file was renamed.
ImageType image_detect(const uint8_t* bytes, size_t len);
