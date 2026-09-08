# AP-Mode Provisioning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move all WiFi/MQTT/updater credentials out of compile-time build flags into an EEPROM-backed runtime config, provisioned over a WPA2 access point, so published firmware binaries contain no secrets.

**Architecture:** All non-trivial logic lives in Arduino-free translation units (`config_codec`, `image_detect`, `net_util`, `net_decide`) that compile and unit-test on the host via `pio test -e native`. Hardware modules (`config_store`, `net_manager`, `web_server`, `mqtt_client`) are thin glue over those pure cores. `main.cpp` only wires modules together.

**Tech Stack:** PlatformIO, Arduino ESP8266 core, LittleFS, EEPROM emulation, PubSubClient, ArduinoJson 7, DNSServer, Unity (native tests).

**Spec:** `docs/superpowers/specs/2026-09-08-ap-provisioning-design.md`

## Global Constraints

- Target board `nodemcuv2`, ldscript `eagle.flash.4m1m.ld`, filesystem `littlefs`.
- `CONFIG_MAGIC` is `0xC0FFEE01`; `CONFIG_VERSION` is `1`.
- `sizeof(Config)` must be exactly 440 bytes; `EEPROM.begin(512)`.
- CRC32 is computed over exactly `offsetof(Config, crc32)` bytes, never `sizeof(Config)`.
- Every `Config` instance must be zero-filled before any field is written.
- `GET /config` must never return a stored password. Empty password on `POST /config` means "keep existing".
- Auth falls back to `admin`/`admin` only when the stored config is invalid.
- After Task 10, `strings .pio/build/nodemcuv2/firmware.bin` must contain no WiFi PSK, MQTT credential, or updater password.
- Only these build flags survive: `VERSION_MAJOR`, `VERSION_MINOR`, `VERSION_PATCH`, `AP_SSID_PREFIX`, `AP_PASSWORD`.
- Target version is 1.6.0.
- Commit after every task. Never commit `release/` or `platformio.ini`.

## File Structure

**Arduino-free (compiled into both envs, unit-tested natively):**

| File | Responsibility |
|---|---|
| `include/config_codec.h` / `src/config_codec.cpp` | `Config` struct, CRC32, zeroing, serialize/deserialize to a byte blob |
| `include/image_detect.h` / `src/image_detect.cpp` | Firmware-vs-filesystem detection by magic byte |
| `include/net_util.h` / `src/net_util.cpp` | Broadcast address arithmetic |
| `include/net_decide.h` / `src/net_decide.cpp` | Boot-state decision function |

**Hardware glue (nodemcuv2 env only):**

| File | Responsibility |
|---|---|
| `include/config_store.h` / `src/config_store.cpp` | EEPROM read/write around `config_codec` |
| `include/net_manager.h` / `src/net_manager.cpp` | AP/STA state machine, captive portal, FLASH button |
| `include/web_server.h` / `src/web_server.cpp` | HTTP routes, failsafe HTML |
| `include/mqtt_client.h` / `src/mqtt_client.cpp` | Broker connection, LWT, command dispatch |
| `include/pulse_action.h` / `src/pulse_action.cpp` | Relay pulse timers |
| `src/main.cpp` | `setup()` / `loop()` wiring only |

**Tests:** `test/test_config_codec/`, `test/test_image_detect/`, `test/test_net_util/`, `test/test_net_decide/`

**Web assets:** `data/index.html`, `data/index.js`, `data/styles.css`

---

### Task 1: Native test harness and config codec

**Files:**
- Create: `include/config_codec.h`, `src/config_codec.cpp`
- Create: `test/test_config_codec/test_config_codec.cpp`
- Modify: `platformio.ini` (add `[env:native]`)

**Interfaces:**
- Consumes: nothing.
- Produces: `struct Config`; `uint32_t crc32_compute(const uint8_t*, size_t)`; `void config_set_defaults(Config&)`; `void config_serialize(const Config&, uint8_t* buf)`; `bool config_deserialize(const uint8_t* buf, Config& out)`; `bool config_is_provisioned(const Config&)`; constants `CONFIG_MAGIC`, `CONFIG_VERSION`, `CONFIG_BLOB_SIZE`.

- [ ] **Step 1: Add the native test environment**

Append to `platformio.ini`. Do not touch the existing `[env:nodemcuv2]` section yet.

```ini
[env:native]
platform = native
test_framework = unity
test_build_src = yes
build_src_filter = -<*> +<config_codec.cpp> +<image_detect.cpp> +<net_util.cpp> +<net_decide.cpp>
build_flags = -std=gnu++17 -Iinclude
```

`build_src_filter` lists all four pure modules now so later tasks need no `platformio.ini` edits. Tasks 1 and 3 create them; until then the filter silently matches nothing for missing files.

- [ ] **Step 2: Write the failing test**

Create `test/test_config_codec/test_config_codec.cpp`:

```cpp
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

    // clean: built the normal way, through config_set_defaults.
    config_set_defaults(clean);
    strcpy(clean.wifi_ssid, "TestNet");
    clean.mqtt_port = 8884;

    // dirty: same logical field values, but the padding between members is
    // left holding 0xAB. config_set_defaults is deliberately NOT called here —
    // it memsets the whole object, which would scrub the very bytes this test
    // needs dirty. Every named field is cleared and set individually instead.
    memset(&dirty, 0xAB, sizeof(dirty));
    dirty.magic = CONFIG_MAGIC;
    dirty.version = CONFIG_VERSION;
    dirty.mqtt_port = 8884;
    dirty.ap_forced = false;
    memset(dirty.wifi_ssid, 0, sizeof(dirty.wifi_ssid));
    strcpy(dirty.wifi_ssid, "TestNet");
    memset(dirty.wifi_psk,  0, sizeof(dirty.wifi_psk));
    memset(dirty.mqtt_host, 0, sizeof(dirty.mqtt_host));
    memset(dirty.mqtt_user, 0, sizeof(dirty.mqtt_user));
    memset(dirty.mqtt_pass, 0, sizeof(dirty.mqtt_pass));
    memset(dirty.device_id, 0, sizeof(dirty.device_id));
    memset(dirty.mdns_host, 0, sizeof(dirty.mdns_host));
    strcpy(dirty.mdns_host, "esp-updater");
    memset(dirty.upd_user,  0, sizeof(dirty.upd_user));
    strcpy(dirty.upd_user, "admin");
    memset(dirty.upd_pass,  0, sizeof(dirty.upd_pass));
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
```

`test_serialize_ignores_caller_padding` is the regression guard for the padding defect identified during spec review: two logically identical configs must produce byte-identical blobs regardless of what the padding between their members happens to hold.

An earlier draft of this plan specified a test named `test_serialize_zeroes_padding_deterministically`, which dirtied a `Config` with `memset(&b, 0xAB, sizeof(b))` and then called `config_set_defaults(b)`. That test was vacuous and could never fail: `config_set_defaults` memsets the entire object, so it erased the very padding the test meant to dirty, and the two blobs were guaranteed identical no matter how `config_serialize` behaved. The shipped test therefore skips `config_set_defaults` on the dirty copy and clears and assigns each named field by hand, leaving the inter-member padding at `0xAB`. That is what actually exercises the field-by-field serialiser: if `config_serialize` ever regressed to a raw `memcpy` of the struct, this version fails and the original would not.

- [ ] **Step 3: Run the test to verify it fails**

Run: `pio test -e native`
Expected: FAIL — `config_codec.h: No such file or directory`.

- [ ] **Step 4: Write the header**

Create `include/config_codec.h`:

```cpp
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
```

- [ ] **Step 5: Write the implementation**

Create `src/config_codec.cpp`:

```cpp
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
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `pio test -e native`
Expected: PASS, 9 tests.

If `test_struct_layout_is_440_bytes` fails, the host ABI differs from the device ABI. Stop and report — the on-flash format would not be portable.

- [ ] **Step 7: Commit**

```bash
git add platformio.ini.sample include/config_codec.h src/config_codec.cpp test/test_config_codec/
git commit -m "feat: add EEPROM config codec with CRC32 validation

Pure, Arduino-free so it unit-tests on the host. Field-by-field copy
and explicit zeroing keep struct padding out of the CRC."
```

Note: `platformio.ini` is gitignored, so the `[env:native]` block must also be added to `platformio.ini.sample` in this step so the change is tracked.

---

### Task 2: EEPROM-backed config store

**Files:**
- Create: `include/config_store.h`, `src/config_store.cpp`

**Interfaces:**
- Consumes: `config_codec.h` — `Config`, `config_serialize`, `config_deserialize`, `config_set_defaults`, `CONFIG_BLOB_SIZE`.
- Produces: `void config_store_begin()`; `Config& config()`; `bool config_store_save()`; `void config_store_factory_reset()`; `bool config_store_is_valid()`.

There is no native test for this task: it is ~50 lines of EEPROM glue over logic already covered by Task 1. It is verified by the Task 10 hardware matrix.

- [ ] **Step 1: Write the header**

Create `include/config_store.h`:

```cpp
#pragma once
#include "config_codec.h"

// Reads EEPROM into the in-RAM config. On invalid/blank EEPROM the config is
// reset to defaults and left unprovisioned. Call once from setup().
void config_store_begin();

// The live in-RAM config. Mutate, then call config_store_save().
Config& config();

// Serialises the live config to EEPROM. Returns EEPROM.commit()'s result.
bool config_store_save();

// Zeroes the EEPROM blob and resets the live config to defaults.
void config_store_factory_reset();

// True when the config loaded from EEPROM passed magic+CRC validation.
bool config_store_is_valid();
```

- [ ] **Step 2: Write the implementation**

Create `src/config_store.cpp`:

```cpp
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
```

- [ ] **Step 3: Verify it compiles**

Run: `pio run -e nodemcuv2`
Expected: compiles. `main.cpp` does not reference the module yet, so behaviour is unchanged.

- [ ] **Step 4: Commit**

```bash
git add include/config_store.h src/config_store.cpp
git commit -m "feat: add EEPROM-backed config store

Thin glue over config_codec. Survives firmware and filesystem updates
because ESP8266 EEPROM emulation owns a dedicated flash sector."
```

---

### Task 3: Pure helpers for three defect fixes

**Files:**
- Create: `include/image_detect.h`, `src/image_detect.cpp`
- Create: `include/net_util.h`, `src/net_util.cpp`
- Create: `include/net_decide.h`, `src/net_decide.cpp`
- Create: `test/test_image_detect/test_image_detect.cpp`
- Create: `test/test_net_util/test_net_util.cpp`
- Create: `test/test_net_decide/test_net_decide.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `enum ImageType { IMAGE_FILESYSTEM = 0, IMAGE_FIRMWARE = 1 }`; `ImageType image_detect(const uint8_t* bytes, size_t len)`; `uint32_t broadcast_addr(uint32_t ip, uint32_t netmask)`; `enum NetState { NET_STA_CONNECTING = 0, NET_AP = 1 }`; `NetState net_initial_state(bool provisioned, bool ap_forced, bool reset_requested)`.

- [ ] **Step 1: Write the failing tests**

Create `test/test_image_detect/test_image_detect.cpp`:

```cpp
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
```

Create `test/test_net_util/test_net_util.cpp`:

```cpp
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
```

Create `test/test_net_decide/test_net_decide.cpp`:

```cpp
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
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pio test -e native`
Expected: FAIL — `image_detect.h: No such file or directory`.

- [ ] **Step 3: Write the implementations**

Create `include/image_detect.h`:

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

enum ImageType { IMAGE_FILESYSTEM = 0, IMAGE_FIRMWARE = 1 };

// ESP8266 firmware images begin with 0xE9. Anything else is treated as a
// filesystem image. Replaces the previous filename-substring heuristic, which
// bricked the device when a firmware file was renamed.
ImageType image_detect(const uint8_t* bytes, size_t len);
```

Create `src/image_detect.cpp`:

```cpp
#include "image_detect.h"

ImageType image_detect(const uint8_t* bytes, size_t len) {
    if (bytes == nullptr || len < 1) return IMAGE_FILESYSTEM;
    return bytes[0] == 0xE9 ? IMAGE_FIRMWARE : IMAGE_FILESYSTEM;
}
```

Create `include/net_util.h`:

```cpp
#pragma once
#include <stdint.h>

// Directed broadcast for the given address/netmask, in the same byte order as
// the inputs. Callers pass ESP8266 IPAddress values via their uint32_t form.
uint32_t broadcast_addr(uint32_t ip, uint32_t netmask);
```

Create `src/net_util.cpp`:

```cpp
#include "net_util.h"

uint32_t broadcast_addr(uint32_t ip, uint32_t netmask) {
    return ip | ~netmask;
}
```

Create `include/net_decide.h`:

```cpp
#pragma once

enum NetState { NET_STA_CONNECTING = 0, NET_AP = 1 };

// Which network state to enter at boot. AP wins whenever the device cannot or
// should not attempt a station connection.
NetState net_initial_state(bool provisioned, bool ap_forced, bool reset_requested);
```

Create `src/net_decide.cpp`:

```cpp
#include "net_decide.h"

NetState net_initial_state(bool provisioned, bool ap_forced, bool reset_requested) {
    if (reset_requested) return NET_AP;
    if (ap_forced)       return NET_AP;
    if (!provisioned)    return NET_AP;
    return NET_STA_CONNECTING;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `pio test -e native`
Expected: PASS, 21 tests total across four suites.

- [ ] **Step 5: Commit**

```bash
git add include/image_detect.h src/image_detect.cpp \
        include/net_util.h src/net_util.cpp \
        include/net_decide.h src/net_decide.cpp \
        test/test_image_detect/ test/test_net_util/ test/test_net_decide/
git commit -m "feat: add pure helpers for image detection, broadcast and boot state

image_detect replaces the filename-substring heuristic that bricked the
device on a renamed firmware file. broadcast_addr replaces the hardcoded
10.10.10.255. All three unit-tested on the host."
```

---
### Task 4: Extract PulseAction into its own module

**Files:**
- Create: `include/pulse_action.h`, `src/pulse_action.cpp`
- Modify: `src/main.cpp` (remove the struct definition, add the include)

**Interfaces:**
- Consumes: nothing.
- Produces: `struct PulseAction` with `void init(unsigned int pin, const char* name)`, `void trigger(unsigned long ms, const char* init_msg, const char* update_msg, const char* active_msg)`, `void update()`.

Pure refactor. Behaviour must not change. The existing struct at `src/main.cpp:53-100` is already correct — including its overflow-safe `millis()` arithmetic — so it moves verbatim apart from the status-publish calls, which become a callback so the module does not depend on MQTT.

- [ ] **Step 1: Write the header**

Create `include/pulse_action.h`:

```cpp
#pragma once

// Publishes a status string. Injected so this module does not depend on MQTT.
typedef void (*PulseStatusFn)(const char* message);

struct PulseAction {
    unsigned int  pin;
    unsigned long timer;
    unsigned long duration;
    bool          active;
    const char*   name;
    const char*   update_message;
    PulseStatusFn publish;

    // Board logic is inverted: HIGH means off.
    void init(unsigned int _pin, const char* _name, PulseStatusFn _publish);
    void trigger(unsigned long ms, const char* init_message,
                 const char* _update_message, const char* active_message);
    void update();
};
```

- [ ] **Step 2: Write the implementation**

Create `src/pulse_action.cpp`:

```cpp
#include "pulse_action.h"
#include <Arduino.h>

void PulseAction::init(unsigned int _pin, const char* _name, PulseStatusFn _publish) {
    pin      = _pin;
    name     = _name;
    publish  = _publish;
    timer    = 0;
    active   = false;
    duration = 0;
    update_message = nullptr;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);   // off
}

void PulseAction::trigger(unsigned long ms, const char* init_message,
                          const char* _update_message, const char* active_message) {
    if (active) {
        if (publish) publish(active_message);
        return;
    }
    if (publish) publish(init_message);
    digitalWrite(pin, LOW);    // on
    timer          = millis();
    duration       = ms;
    active         = true;
    update_message = _update_message;
}

void PulseAction::update() {
    if (active && (millis() - timer) >= duration) {
        digitalWrite(pin, HIGH);   // off
        active = false;
        if (publish && update_message) publish(update_message);
    }
}
```

- [ ] **Step 3: Update main.cpp**

Delete the `struct PulseAction { ... };` block at `src/main.cpp:53-100`. Add `#include "pulse_action.h"` near the top. Change the two `init` calls in `setup()` to pass a publish function:

```cpp
static void publish_status(const char* message) {
    client.publish(topic_status, message);
}
// ...
win_server.init(POWER_PIN_WIN_SERVER, "win-server", publish_status);
nas_server.init(POWER_PIN_NAS_SERVER, "nas-server", publish_status);
```

- [ ] **Step 4: Verify it builds and behaviour is unchanged**

Run: `pio run -e nodemcuv2`
Expected: compiles. Flash and confirm `POWER_ON_WIN_SERVER` still pulses D5 for 500ms and `FORCE_POWER_OFF_WIN_SERVER` for 5000ms.

- [ ] **Step 5: Commit**

```bash
git add include/pulse_action.h src/pulse_action.cpp src/main.cpp
git commit -m "refactor: extract PulseAction into its own module

Status publishing becomes an injected callback so the relay timer
module does not depend on MQTT. No behaviour change."
```

---

### Task 5: Network state machine, AP mode and captive portal

**Files:**
- Create: `include/net_manager.h`, `src/net_manager.cpp`
- Modify: `src/main.cpp` (replace `setup_wifi()`)

**Interfaces:**
- Consumes: `net_decide.h` (`NetState`, `net_initial_state`), `config_store.h` (`config()`, `config_store_is_valid`, `config_store_save`, `config_store_factory_reset`).
- Produces: `void net_begin()`; `void net_loop()`; `NetState net_state()`; `bool net_is_ap()`; `const char* net_ap_ssid()`; `void net_set_ap_forced(bool)`; `void net_factory_reset_and_reboot()`.

- [ ] **Step 1: Write the header**

Create `include/net_manager.h`:

```cpp
#pragma once
#include "net_decide.h"

// Reads the FLASH button, decides the boot state, brings up AP or STA.
void net_begin();

// Drives the state machine. Call every loop().
void net_loop();

NetState    net_state();
bool        net_is_ap();
const char* net_ap_ssid();

// Persists ap_forced then reboots into the requested mode.
void net_set_ap_forced(bool forced);

// Wipes EEPROM then reboots into AP.
void net_factory_reset_and_reboot();
```

- [ ] **Step 2: Write the implementation**

Create `src/net_manager.cpp`:

```cpp
#include "net_manager.h"
#include "config_store.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>

static const unsigned long STA_CONNECT_TIMEOUT_MS = 30000;
static const unsigned long AP_STA_RETRY_MS        = 300000;   // 5 minutes
static const unsigned long RESET_HOLD_MS          = 5000;
static const uint8_t       FLASH_BUTTON_PIN       = 0;         // GPIO0
static const byte          DNS_PORT               = 53;

static NetState     g_state          = NET_AP;
static unsigned long g_state_entered = 0;
static unsigned long g_last_retry    = 0;
static char          g_ap_ssid[33]   = {0};
static DNSServer     g_dns;
static bool          g_dns_active    = false;

// The FLASH button cannot be sampled during power-on: holding GPIO0 low at
// reset enters the ROM bootloader. Sample after boot instead, so the user
// presses and holds within the first five seconds of running firmware.
static bool flash_button_held_for_reset() {
    pinMode(FLASH_BUTTON_PIN, INPUT_PULLUP);
    if (digitalRead(FLASH_BUTTON_PIN) != LOW) return false;

    Serial.println(F("FLASH held; hold 5s for factory reset..."));
    const unsigned long start = millis();
    while (millis() - start < RESET_HOLD_MS) {
        if (digitalRead(FLASH_BUTTON_PIN) != LOW) return false;
        digitalWrite(LED_BUILTIN, LOW);
        delay(50);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(50);
    }
    Serial.println(F("Factory reset triggered."));
    return true;
}

static void build_ap_ssid() {
    snprintf(g_ap_ssid, sizeof(g_ap_ssid), "%s-%06X", AP_SSID_PREFIX, ESP.getChipId());
}

static void enter_ap() {
    build_ap_ssid();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(g_ap_ssid, AP_PASSWORD);
    const IPAddress ip = WiFi.softAPIP();

    g_dns.setErrorReplyCode(DNSReplyCode::NoError);
    g_dns_active = g_dns.start(DNS_PORT, "*", ip);

    g_state         = NET_AP;
    g_state_entered = millis();
    g_last_retry    = millis();

    Serial.printf("AP up: %s  http://%s\n", g_ap_ssid, ip.toString().c_str());
}

static void enter_sta() {
    if (g_dns_active) { g_dns.stop(); g_dns_active = false; }
    WiFi.mode(WIFI_STA);
    WiFi.begin(config().wifi_ssid, config().wifi_psk);
    g_state         = NET_STA_CONNECTING;
    g_state_entered = millis();
    Serial.printf("Connecting to %s\n", config().wifi_ssid);
}

void net_begin() {
    const bool reset_requested = flash_button_held_for_reset();
    if (reset_requested) {
        config_store_factory_reset();
    }
    const NetState want = net_initial_state(
        config_is_provisioned(config()), config().ap_forced, reset_requested);

    if (want == NET_AP) enter_ap();
    else                enter_sta();
}

void net_loop() {
    if (g_dns_active) g_dns.processNextRequest();

    switch (g_state) {
    case NET_STA_CONNECTING:
        if (WiFi.status() == WL_CONNECTED) {
            g_state         = NET_STA_CONNECTED;
            g_state_entered = millis();
            Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
            for (int i = 0; i < 3; i++) {          // connected blink
                digitalWrite(LED_BUILTIN, LOW);  delay(50);
                digitalWrite(LED_BUILTIN, HIGH); delay(50);
            }
        } else if (millis() - g_state_entered >= STA_CONNECT_TIMEOUT_MS) {
            Serial.println(F("STA timeout; falling back to AP."));
            enter_ap();
        }
        break;

    case NET_STA_CONNECTED:
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("WiFi lost; reconnecting."));
            enter_sta();
        }
        break;

    case NET_AP:
        // Self-heal: retry the station connection periodically unless the user
        // explicitly pinned AP mode.
        if (!config().ap_forced && config_is_provisioned(config()) &&
            millis() - g_last_retry >= AP_STA_RETRY_MS) {
            g_last_retry = millis();
            Serial.println(F("Retrying STA from AP mode."));
            enter_sta();
        }
        break;
    }
}

NetState    net_state()   { return g_state; }
bool        net_is_ap()   { return g_state == NET_AP; }
const char* net_ap_ssid() { return g_ap_ssid; }

void net_set_ap_forced(bool forced) {
    config().ap_forced = forced;
    config_store_save();
    delay(200);
    ESP.restart();
}

void net_factory_reset_and_reboot() {
    config_store_factory_reset();
    delay(200);
    ESP.restart();
}
```

- [ ] **Step 3: Wire it into main.cpp**

Delete `setup_wifi()` (`src/main.cpp:125-168`) entirely. In `setup()`, replace the `setup_wifi();` call with `config_store_begin(); net_begin();`, ensuring `config_store_begin()` runs first — `net_begin()` reads the config. Add `net_loop();` as the first statement in `loop()`.

- [ ] **Step 4: Verify**

Run: `pio run -e nodemcuv2`
Then flash and check the serial monitor at 115200:
- With blank EEPROM: `AP up: esp-setup-XXXXXX  http://192.168.4.1`.
- Connect a phone to that SSID using `AP_PASSWORD`; a captive-portal notification should appear.
- Hold FLASH within 5s of boot: `Factory reset triggered.`

- [ ] **Step 5: Commit**

```bash
git add include/net_manager.h src/net_manager.cpp src/main.cpp
git commit -m "feat: add non-blocking AP/STA state machine with captive portal

Replaces setup_wifi(), which blocked forever when WiFi was unavailable
and prevented the web server from ever starting. Adds 5-minute STA
retry from AP mode and FLASH-button factory reset."
```

---

### Task 6: MQTT module, LWT, buffer sizing and new commands

**Files:**
- Create: `include/mqtt_client.h`, `src/mqtt_client.cpp`
- Modify: `src/main.cpp` (remove `callback()`, `reconnect()`, client globals)

**Interfaces:**
- Consumes: `config_store.h`, `net_manager.h`, `pulse_action.h`.
- Produces: `void mqtt_begin(PulseAction* win, PulseAction* nas)`; `void mqtt_loop()`; `void mqtt_publish_status(const char* msg)`; `bool mqtt_connected()`.

- [ ] **Step 1: Write the header**

Create `include/mqtt_client.h`:

```cpp
#pragma once
#include "pulse_action.h"

void mqtt_begin(PulseAction* win, PulseAction* nas);
void mqtt_loop();
void mqtt_publish_status(const char* message);
bool mqtt_connected();
```

- [ ] **Step 2: Write the implementation**

Create `src/mqtt_client.cpp`. Move `callback()` (`src/main.cpp:170-283`) and `reconnect()` (`src/main.cpp:285-309`) here, applying these changes:

1. Read every credential from `config()` instead of the `-D` macros.
2. Delete the `delay(100)` chains in the `FUCK_YOU` and `DIDDY` branches — they stall `client.loop()` and the MQTT keepalive. Publish the lines back to back.
3. Fix the integer division in the retry log: `mqtt_reconnect_interval / 1000.0`.
4. Rewrite `send_magic_packet` to use `broadcast_addr` and `beginPacket`.
5. Add the four new commands.

```cpp
#include "mqtt_client.h"
#include "config_store.h"
#include "net_manager.h"
#include "net_util.h"
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>

static WiFiUDP          udp;
static WiFiClientSecure espClient;
static PubSubClient     client(espClient);
static PulseAction*     g_win = nullptr;
static PulseAction*     g_nas = nullptr;

static const unsigned long mqtt_reconnect_interval = 5000;
static unsigned long       mqtt_last_attempt = 0;

static const char* topic_command = "osiris/esp8266/command";
static const char* topic_status  = "osiris/esp8266/status";

static const uint8_t SERVER_MAC[6] = {0xC8, 0xD3, 0xFF, 0x6E, 0x9E, 0xF2};

void mqtt_publish_status(const char* message) {
    if (client.connected()) client.publish(topic_status, message);
}

bool mqtt_connected() { return client.connected(); }

static int send_magic_packet(uint16_t port = 9) {
    uint8_t payload[102];
    for (int i = 0; i < 6; i++) payload[i] = 0xFF;
    for (int i = 6; i < 102; i += 6)
        for (int j = 0; j < 6; j++) payload[i + j] = SERVER_MAC[j];

    // Directed broadcast derived from the live subnet. The previous code used
    // beginPacketMulticast against a hardcoded 10.10.10.255, which is not a
    // multicast address and broke on any other subnet.
    const IPAddress bcast(broadcast_addr((uint32_t)WiFi.localIP(),
                                         (uint32_t)WiFi.subnetMask()));
    udp.beginPacket(bcast, port);
    udp.write(payload, sizeof(payload));
    return udp.endPacket();
}

static void callback(char* topic, byte* payload, unsigned int length) {
    String message;
    message.reserve(length);
    for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
    Serial.printf("[%s] %s\n", topic, message.c_str());

    if (String(topic) != topic_command) return;

    if (message == "AP_MODE_ON") {
        client.publish(topic_status, "Switching to AP mode, hold tight...");
        net_set_ap_forced(true);              // saves and reboots
    } else if (message == "AP_MODE_OFF") {
        client.publish(topic_status, "Leaving AP mode, reconnecting...");
        net_set_ap_forced(false);
    } else if (message == "FACTORY_RESET") {
        client.publish(topic_status, "Wiping config, back to setup mode.");
        net_factory_reset_and_reboot();
    } else if (message == "WIFI_STATUS") {
        String s = String("state=") + (net_is_ap() ? "AP" : "STA") +
                   " ssid=" + (net_is_ap() ? net_ap_ssid() : WiFi.SSID().c_str()) +
                   " rssi=" + String(WiFi.RSSI()) +
                   " ip="   + (net_is_ap() ? WiFi.softAPIP().toString()
                                           : WiFi.localIP().toString());
        client.publish(topic_status, s.c_str());
    }
    // ... existing PING / VERSION / POWER_* / MAGIC_WAKE_NAS / REBOOT / RESET
    // branches follow unchanged, minus the delay(100) chains.
}

static void reconnect() {
    Serial.print(F("Attempting MQTT connection..."));
    // Last Will: the broker publishes this if the device drops without a clean
    // disconnect, so the status topic never shows a stale "online".
    const bool ok = client.connect(config().device_id,
                                   config().mqtt_user, config().mqtt_pass,
                                   topic_status, 0, true, "offline");
    if (ok) {
        Serial.println(F("connected"));
        client.publish(topic_status, "online", true);   // retained
        client.subscribe(topic_command);
    } else {
        Serial.printf(" failed, rc=%d, retry in %.2f s\n",
                      client.state(), mqtt_reconnect_interval / 1000.0);
    }
}

void mqtt_begin(PulseAction* win, PulseAction* nas) {
    g_win = win;
    g_nas = nas;
    espClient.setInsecure();
    // Default TLS buffers are ~16KB each and exhaust the heap alongside the
    // web server. MQTT frames here are far smaller.
    espClient.setBufferSizes(1024, 1024);
    client.setServer(config().mqtt_host, config().mqtt_port);
    client.setCallback(callback);
    // Default PubSubClient buffer is 256 bytes; longer status strings were
    // being dropped silently.
    client.setBufferSize(512);
}

void mqtt_loop() {
    if (net_is_ap()) return;              // no broker while provisioning
    const unsigned long now = millis();
    if (now - mqtt_last_attempt > mqtt_reconnect_interval) {
        mqtt_last_attempt = now;
        if (!client.connected()) reconnect();
    }
    client.loop();
}
```

The `PING`, `VERSION`, `POWER_ON_*`, `FORCE_POWER_OFF_*`, `MAGIC_WAKE_NAS`, `REBOOT`, `RESET` and novelty branches move across verbatim from `src/main.cpp:187-281`, using `g_win`/`g_nas` in place of the file-scope `win_server`/`nas_server`.

- [ ] **Step 3: Verify**

Run: `pio run -e nodemcuv2`
Flash, then with the device provisioned: publish `WIFI_STATUS` to `osiris/esp8266/command` and confirm a status reply. Kill power and confirm the broker publishes `offline`.

- [ ] **Step 4: Commit**

```bash
git add include/mqtt_client.h src/mqtt_client.cpp src/main.cpp
git commit -m "feat: extract MQTT module, add LWT and AP-control commands

Credentials now come from the runtime config. Adds AP_MODE_ON/OFF,
FACTORY_RESET and WIFI_STATUS. Fixes the WOL broadcast, raises the
PubSubClient buffer to 512B, caps TLS buffers at 1KB, and removes the
delay() chains that stalled the MQTT keepalive."
```

---

### Task 7: Web server module and security fixes

**Files:**
- Create: `include/web_server.h`, `src/web_server.cpp`
- Modify: `src/main.cpp` (remove `setup_webupdater()` and the server global)

**Interfaces:**
- Consumes: `config_store.h`, `net_manager.h`, `image_detect.h`.
- Produces: `void web_begin()`; `void web_loop()`; `bool web_require_auth()`.

This task moves the existing routes and applies four security fixes. The provisioning endpoints come in Task 8.

- [ ] **Step 1: Write the header**

Create `include/web_server.h`:

```cpp
#pragma once

void web_begin();
void web_loop();

// Sends a 401 and returns false when the request is unauthenticated.
// Credentials come from the config, falling back to admin/admin when the
// stored config is invalid.
bool web_require_auth();
```

- [ ] **Step 2: Move the routes and apply the fixes**

Create `src/web_server.cpp`. Move `setup_webupdater()` (`src/main.cpp:311-509`) here, applying:

**Fix 1 — auth reads from config, with fallback:**

```cpp
bool web_require_auth() {
    const char* user = config_store_is_valid() ? config().upd_user : "admin";
    const char* pass = config_store_is_valid() ? config().upd_pass : "admin";
    if (!http_server.authenticate(user, pass)) {
        http_server.requestAuthentication();
        return false;
    }
    return true;
}
```

Every route replaces its inline `if (!http_server.authenticate(...)) return http_server.requestAuthentication();` with `if (!web_require_auth()) return;`.

**Fix 2 — CSRF check on state-changing POSTs.** Basic-auth credentials are sent automatically by the browser, so any page could POST to `/update` or `/reboot`:

```cpp
// Rejects cross-site POSTs. Same-origin requests either omit Origin or send
// one matching this device.
static bool same_origin() {
    if (!http_server.hasHeader("Origin")) return true;   // curl, non-browser
    const String origin = http_server.header("Origin");
    const String host   = http_server.hostHeader();
    return origin.endsWith(host);
}

static bool guard_post() {
    if (!web_require_auth()) return false;
    if (!same_origin()) {
        http_server.send(403, "text/plain", "Cross-origin POST rejected");
        return false;
    }
    return true;
}
```

`http_server.collectHeaders("Origin")` must be called in `web_begin()` before `http_server.begin()`, otherwise `hasHeader("Origin")` always returns false.

Apply `guard_post()` to `/update`, `/reboot`, and the Task 8 endpoints.

**Fix 3 — path traversal guard in `onNotFound`:**

```cpp
String path = http_server.uri();
if (path.indexOf("..") >= 0) {
    http_server.send(400, "text/plain", "Bad path");
    return;
}
```

**Fix 4 — magic-byte image detection.** In the `/update` upload handler, replace the filename-substring test. `Update.begin()` moves out of `UPLOAD_FILE_START` because the first byte is not available until the first write:

```cpp
static bool   update_started = false;
static size_t fs_size_for_update() {
    extern uint32_t _FS_start;
    extern uint32_t _FS_end;
    return (size_t)&_FS_end - (size_t)&_FS_start;
}

// inside the upload handler:
if (upload.status == UPLOAD_FILE_START) {
    update_started = false;
    Serial.printf("Update: %s\n", upload.filename.c_str());
} else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!update_started) {
        const ImageType type = image_detect(upload.buf, upload.currentSize);
        const int    command = (type == IMAGE_FIRMWARE) ? U_FLASH : U_FS;
        const size_t size    = (type == IMAGE_FIRMWARE)
                             ? ((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)
                             : fs_size_for_update();
        Serial.printf("Target: %s\n", type == IMAGE_FIRMWARE ? "Firmware" : "Filesystem");
        if (!Update.begin(size, command)) { Update.printError(Serial); return; }
        update_started = true;
    }
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
    }
} else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("Success: %u bytes\n", upload.totalSize);
    else                  Update.printError(Serial);
}
```

Also change `MDNS.begin(mdns_hostname)` to `MDNS.begin(config().mdns_host)`.

- [ ] **Step 3: Verify**

Run: `pio run -e nodemcuv2`
Flash, then:
- Browse to the device; the updater page loads after auth.
- `curl -u admin:admin -X POST -H "Origin: http://evil.test" http://<ip>/reboot` returns 403.
- `curl -u admin:admin -X POST http://<ip>/reboot` succeeds.
- Rename a firmware `.bin` to `nigga_filesystem_test.bin` and upload it; serial must report `Target: Firmware`.

- [ ] **Step 4: Commit**

```bash
git add include/web_server.h src/web_server.cpp src/main.cpp
git commit -m "feat: extract web server, detect images by magic byte

Auth now reads from the runtime config. Adds an Origin check on
state-changing POSTs, a path-traversal guard on the static handler,
and replaces filename-substring image detection with the 0xE9 magic
byte, which previously bricked the device on a renamed file."
```

---
### Task 8: Provisioning endpoints and failsafe form

**Files:**
- Modify: `src/web_server.cpp` (add four routes, extend the failsafe page)

**Interfaces:**
- Consumes: `config_store.h`, `net_manager.h`, `web_server.h` (`web_require_auth`), ArduinoJson.
- Produces: routes `GET /config`, `POST /config`, `POST /ap_mode`, `POST /factory_reset`.

- [ ] **Step 1: Add the field-copy helpers**

Add to `src/web_server.cpp`. The distinction between the two is the whole password-redaction contract: a secret arriving empty means "keep what is stored", so the browser never has to hold or resend it.

```cpp
static void copy_field(char* dst, size_t cap, JsonVariant v) {
    if (v.isNull()) return;
    const char* s = v.as<const char*>();
    if (s == nullptr) return;
    strncpy(dst, s, cap - 1);
    dst[cap - 1] = '\0';
}

static void copy_secret(char* dst, size_t cap, JsonVariant v) {
    if (v.isNull()) return;
    const char* s = v.as<const char*>();
    if (s == nullptr || s[0] == '\0') return;   // empty means keep existing
    strncpy(dst, s, cap - 1);
    dst[cap - 1] = '\0';
}
```

- [ ] **Step 2: Add GET /config**

```cpp
http_server.on("/config", HTTP_GET, []() {
    if (!web_require_auth()) return;
    const Config& c = config();
    JsonDocument doc;
    doc["wifi_ssid"]     = c.wifi_ssid;
    doc["has_wifi_psk"]  = c.wifi_psk[0]  != '\0';
    doc["mqtt_host"]     = c.mqtt_host;
    doc["mqtt_port"]     = c.mqtt_port;
    doc["mqtt_user"]     = c.mqtt_user;
    doc["has_mqtt_pass"] = c.mqtt_pass[0] != '\0';
    doc["device_id"]     = c.device_id;
    doc["mdns_host"]     = c.mdns_host;
    doc["upd_user"]      = c.upd_user;
    doc["has_upd_pass"]  = c.upd_pass[0]  != '\0';
    doc["ap_forced"]     = c.ap_forced;
    doc["ap_mode"]       = net_is_ap();
    doc["ap_ssid"]       = net_ap_ssid();
    doc["provisioned"]   = config_is_provisioned(c);
    doc["default_creds"] = (strcmp(c.upd_user, "admin") == 0 &&
                            strcmp(c.upd_pass, "admin") == 0);
    String out;
    serializeJson(doc, out);
    http_server.send(200, "application/json", out);
});
```

No password field is ever emitted — only the `has_*` presence booleans.

- [ ] **Step 3: Add POST /config**

```cpp
http_server.on("/config", HTTP_POST, []() {
    if (!guard_post()) return;
    JsonDocument doc;
    if (deserializeJson(doc, http_server.arg("plain"))) {
        http_server.send(400, "application/json", "{\"error\":\"malformed json\"}");
        return;
    }
    Config& c = config();
    copy_field (c.wifi_ssid, sizeof(c.wifi_ssid), doc["wifi_ssid"]);
    copy_secret(c.wifi_psk,  sizeof(c.wifi_psk),  doc["wifi_psk"]);
    copy_field (c.mqtt_host, sizeof(c.mqtt_host), doc["mqtt_host"]);
    copy_field (c.mqtt_user, sizeof(c.mqtt_user), doc["mqtt_user"]);
    copy_secret(c.mqtt_pass, sizeof(c.mqtt_pass), doc["mqtt_pass"]);
    copy_field (c.device_id, sizeof(c.device_id), doc["device_id"]);
    copy_field (c.mdns_host, sizeof(c.mdns_host), doc["mdns_host"]);
    copy_field (c.upd_user,  sizeof(c.upd_user),  doc["upd_user"]);
    copy_secret(c.upd_pass,  sizeof(c.upd_pass),  doc["upd_pass"]);
    if (doc["mqtt_port"].is<unsigned short>()) c.mqtt_port = doc["mqtt_port"];

    if (c.wifi_ssid[0] == '\0') {
        http_server.send(400, "application/json", "{\"error\":\"wifi_ssid required\"}");
        return;
    }
    if (!config_store_save()) {
        http_server.send(500, "application/json", "{\"error\":\"eeprom write failed\"}");
        return;
    }
    http_server.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    delay(500);
    ESP.restart();
});
```

- [ ] **Step 4: Add POST /ap_mode and POST /factory_reset**

```cpp
http_server.on("/ap_mode", HTTP_POST, []() {
    if (!guard_post()) return;
    JsonDocument doc;
    if (deserializeJson(doc, http_server.arg("plain"))) {
        http_server.send(400, "application/json", "{\"error\":\"malformed json\"}");
        return;
    }
    const bool enabled = doc["enabled"] | false;
    http_server.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    delay(500);
    net_set_ap_forced(enabled);     // saves config, then restarts
});

http_server.on("/factory_reset", HTTP_POST, []() {
    if (!guard_post()) return;
    http_server.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    delay(500);
    net_factory_reset_and_reboot();
});
```

- [ ] **Step 5: Extend the failsafe page with a provisioning form**

Replace the `failsafe_html` string in the `/` handler. A freshly flashed device has both an empty EEPROM and an empty LittleFS, so this compiled-in page is the only interface available — it must be able to provision, not just accept uploads.

```cpp
const char* failsafe_html =
    "<!DOCTYPE html><html><head><title>Setup</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:sans-serif;max-width:26rem;margin:2rem auto;padding:0 1rem}"
    "input{width:100%;padding:.5rem;margin:.25rem 0 .75rem;box-sizing:border-box}"
    "button{width:100%;padding:.6rem}</style></head><body>"
    "<h2>Device Setup</h2>"
    "<p>No web assets found. Configure the device below.</p>"
    "<label>WiFi SSID</label><input id=s>"
    "<label>WiFi Password</label><input id=p type=password>"
    "<label>MQTT Host</label><input id=h>"
    "<label>MQTT Port</label><input id=o value=8883>"
    "<label>MQTT User</label><input id=u>"
    "<label>MQTT Password</label><input id=m type=password>"
    "<label>Device ID</label><input id=d>"
    "<button onclick=\"save()\">Save &amp; Reboot</button>"
    "<hr><h3>Upload Firmware or Filesystem</h3>"
    "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<input type='file' name='update' accept='.bin'>"
    "<button type='submit'>Upload</button></form>"
    "<script>function save(){"
    "var b={wifi_ssid:s.value,wifi_psk:p.value,mqtt_host:h.value,"
    "mqtt_port:parseInt(o.value||'8883'),mqtt_user:u.value,"
    "mqtt_pass:m.value,device_id:d.value};"
    "fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify(b)}).then(r=>r.json()).then(j=>{"
    "document.body.innerHTML=j.ok?'<h2>Saved. Rebooting...</h2>':"
    "'<h2>Error: '+(j.error||'unknown')+'</h2>';});}</script>"
    "</body></html>";
```

- [ ] **Step 6: Verify**

Run: `pio run -e nodemcuv2`
Flash with a blank EEPROM and no filesystem, then:
- Join the `esp-setup-XXXXXX` AP and open `http://192.168.4.1`; the setup form appears.
- Submit WiFi and MQTT details; the device reboots and joins the network.
- `curl -u admin:admin http://<ip>/config` returns JSON with `has_wifi_psk: true` and **no** password fields.
- POST a config with `"wifi_psk": ""` and a changed `mqtt_host`; the WiFi password must survive.

- [ ] **Step 7: Commit**

```bash
git add src/web_server.cpp
git commit -m "feat: add provisioning endpoints and failsafe setup form

GET /config redacts every secret and reports presence booleans only;
an empty password on POST keeps the stored value. The compiled-in
failsafe page can now provision a device with an empty filesystem."
```

---

### Task 9: Web UI settings panel

**Files:**
- Modify: `data/index.html`, `data/index.js`, `data/styles.css`

**Interfaces:**
- Consumes: `GET/POST /config`, `POST /ap_mode`, `POST /factory_reset`, `GET /info`.
- Produces: no code interface; UI only.

- [ ] **Step 1: Fix the ReferenceError in index.js**

At `data/index.js:66`, `upload_btn.textContent` throws — the element id is `upload-btn`, and a hyphenated id does not become a JavaScript global. It fires whenever the file selection is cleared.

```javascript
// before
upload_btn.textContent = "Update Nigga";
// after
upload_btn_text.textContent = "Update Nigga";
```

- [ ] **Step 2: Add the settings panel markup**

Insert into `data/index.html` after the closing `</div>` of `#info-container` (line 71), inside the card body:

```html
<div id="settings-container" class="mt-4" style="display:none;">
  <hr>
  <div class="d-flex justify-content-between align-items-center mb-3">
    <h5 class="mb-0">Settings</h5>
    <button class="btn btn-icon-only" onclick="hideSettings()"><i class="bi bi-x-lg"></i></button>
  </div>
  <div id="cred-warning" class="alert alert-warning py-2" style="display:none;">
    Still using the default <code>admin</code>/<code>admin</code> login. Change it below.
  </div>
  <form id="settings-form" class="text-start" onsubmit="saveConfig(event)">
    <label class="form-label">WiFi SSID</label>
    <input class="form-control mb-2" id="cfg-wifi-ssid" required>
    <label class="form-label">WiFi Password <small class="text-muted">(blank = unchanged)</small></label>
    <input class="form-control mb-2" id="cfg-wifi-psk" type="password" placeholder="">
    <label class="form-label">MQTT Host</label>
    <input class="form-control mb-2" id="cfg-mqtt-host">
    <label class="form-label">MQTT Port</label>
    <input class="form-control mb-2" id="cfg-mqtt-port" type="number" min="1" max="65535">
    <label class="form-label">MQTT User</label>
    <input class="form-control mb-2" id="cfg-mqtt-user">
    <label class="form-label">MQTT Password <small class="text-muted">(blank = unchanged)</small></label>
    <input class="form-control mb-2" id="cfg-mqtt-pass" type="password">
    <label class="form-label">Device ID</label>
    <input class="form-control mb-2" id="cfg-device-id">
    <label class="form-label">mDNS Hostname</label>
    <input class="form-control mb-2" id="cfg-mdns-host">
    <label class="form-label">Updater Username</label>
    <input class="form-control mb-2" id="cfg-upd-user">
    <label class="form-label">Updater Password <small class="text-muted">(blank = unchanged)</small></label>
    <input class="form-control mb-3" id="cfg-upd-pass" type="password">

    <div class="form-check form-switch mb-3">
      <input class="form-check-input" type="checkbox" id="cfg-ap-forced" onchange="toggleApMode(this.checked)">
      <label class="form-check-label" for="cfg-ap-forced">Force AP mode</label>
    </div>

    <div class="d-grid gap-2">
      <button type="submit" class="btn btn-update-custom rounded-pill">Save &amp; Reboot</button>
      <button type="button" class="btn btn-outline-danger rounded-pill" onclick="factoryReset()">Factory Reset</button>
    </div>
  </form>
</div>
```

Add a gear button to the top button row (after the info button at line 26):

```html
<button class="btn btn-icon-only" id="btn-settings" onclick="fetchConfig()"
        data-bs-toggle="tooltip" data-bs-placement="top" data-bs-title="Settings">
  <i class="bi bi-gear"></i>
</button>
```

- [ ] **Step 3: Add the settings JavaScript**

Append to `data/index.js`:

```javascript
function fetchConfig() {
  fetch("/config")
    .then(function (r) { return r.json(); })
    .then(function (c) {
      document.getElementById("cfg-wifi-ssid").value = c.wifi_ssid || "";
      document.getElementById("cfg-mqtt-host").value = c.mqtt_host || "";
      document.getElementById("cfg-mqtt-port").value = c.mqtt_port || 8883;
      document.getElementById("cfg-mqtt-user").value = c.mqtt_user || "";
      document.getElementById("cfg-device-id").value = c.device_id || "";
      document.getElementById("cfg-mdns-host").value = c.mdns_host || "";
      document.getElementById("cfg-upd-user").value  = c.upd_user  || "";
      document.getElementById("cfg-ap-forced").checked = !!c.ap_forced;

      // Secrets are never sent by the device. A stored value shows as a
      // placeholder so the field can be left blank to keep it.
      setSecretPlaceholder("cfg-wifi-psk",  c.has_wifi_psk);
      setSecretPlaceholder("cfg-mqtt-pass", c.has_mqtt_pass);
      setSecretPlaceholder("cfg-upd-pass",  c.has_upd_pass);

      document.getElementById("cred-warning").style.display =
        c.default_creds ? "block" : "none";
      document.getElementById("settings-container").style.display = "block";
    })
    .catch(function () { alert("Could not load config."); });
}

function setSecretPlaceholder(id, isSet) {
  var el = document.getElementById(id);
  el.value = "";
  el.placeholder = isSet ? "•••••• (unchanged)" : "not set";
}

function hideSettings() {
  document.getElementById("settings-container").style.display = "none";
}

function saveConfig(event) {
  event.preventDefault();
  var body = {
    wifi_ssid: document.getElementById("cfg-wifi-ssid").value,
    wifi_psk:  document.getElementById("cfg-wifi-psk").value,
    mqtt_host: document.getElementById("cfg-mqtt-host").value,
    mqtt_port: parseInt(document.getElementById("cfg-mqtt-port").value || "8883", 10),
    mqtt_user: document.getElementById("cfg-mqtt-user").value,
    mqtt_pass: document.getElementById("cfg-mqtt-pass").value,
    device_id: document.getElementById("cfg-device-id").value,
    mdns_host: document.getElementById("cfg-mdns-host").value,
    upd_user:  document.getElementById("cfg-upd-user").value,
    upd_pass:  document.getElementById("cfg-upd-pass").value
  };
  postJSON("/config", body, "Settings saved. Rebooting...");
}

function toggleApMode(enabled) {
  if (!confirm(enabled
      ? "Force AP mode? The device will leave your network and reboot."
      : "Leave AP mode and reconnect to WiFi?")) {
    document.getElementById("cfg-ap-forced").checked = !enabled;
    return;
  }
  postJSON("/ap_mode", { enabled: enabled }, "Switching mode. Rebooting...");
}

function factoryReset() {
  if (!confirm("Erase all settings and return to setup mode? This cannot be undone."))
    return;
  postJSON("/factory_reset", {}, "Config erased. Rebooting into AP mode...");
}

function postJSON(url, body, successMsg) {
  fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body)
  })
    .then(function (r) { return r.json().then(function (j) { return { ok: r.ok, j: j }; }); })
    .then(function (res) {
      if (!res.ok) { alert("Error: " + (res.j.error || "unknown")); return; }
      document.getElementById("status").innerText = successMsg;
      setTimeout(function () { location.reload(); }, 15000);
    })
    .catch(function () { alert("Request failed."); });
}
```

- [ ] **Step 4: Verify**

Run: `pio run -e nodemcuv2 -t uploadfs` then reload the page.
- The gear opens the panel; password fields show the unchanged placeholder, never a value.
- Change only the MQTT host and save; confirm over serial that the device still joins WiFi, proving the PSK survived.
- Confirm the default-credential banner appears on `admin`/`admin` and disappears after changing them.
- Clear the file input and confirm the console shows no `ReferenceError`.

- [ ] **Step 5: Commit**

```bash
git add data/index.html data/index.js data/styles.css
git commit -m "feat: add settings panel, AP toggle and factory reset to the UI

Password fields render as placeholders and submit blank to keep the
stored secret. Fixes the upload_btn ReferenceError thrown when the
file selection was cleared."
```

---

### Task 10: Final wiring, build cutover and cleanup

**Files:**
- Modify: `src/main.cpp`, `platformio.ini`, `platformio.ini.sample`, `README.md`
- Delete: `backup/`

**Interfaces:**
- Consumes: every module from Tasks 1-9.
- Produces: the shipping firmware.

- [ ] **Step 1: Reduce main.cpp to wiring**

`src/main.cpp` should end at roughly 90 lines and contain no route handlers, no MQTT logic and no WiFi logic:

```cpp
#include <Arduino.h>
#include <LittleFS.h>
#include "config_store.h"
#include "net_manager.h"
#include "web_server.h"
#include "mqtt_client.h"
#include "pulse_action.h"

// Required for ESP.getVcc() to return a real reading rather than a floating
// ADC. Makes A0 unavailable for external analog input, which this board does
// not use.
ADC_MODE(ADC_VCC);

static const int POWER_PIN_WIN_SERVER = 14;  // GPIO14 (D5)
static const int POWER_PIN_NAS_SERVER = 5;   // GPIO5  (D1)

PulseAction win_server;
PulseAction nas_server;

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    win_server.init(POWER_PIN_WIN_SERVER, "win-server", mqtt_publish_status);
    nas_server.init(POWER_PIN_NAS_SERVER, "nas-server", mqtt_publish_status);

    if (!LittleFS.begin()) {
        Serial.println(F("LittleFS mount failed; failsafe UI will be served."));
    }

    config_store_begin();   // must precede net_begin(); it reads the config
    net_begin();
    web_begin();
    mqtt_begin(&win_server, &nas_server);
}

void loop() {
    net_loop();
    web_loop();
    mqtt_loop();
    win_server.update();
    nas_server.update();
}
```

Confirm the duplicate `#include <ESP8266mDNS.h>` (previously lines 2 and 7) and the unused `#include <wpa2_enterprise.h>` (line 11) are both gone. The latter belongs on the `wpa2_enterprise` branch.

- [ ] **Step 2: Cut over the build flags**

Replace the `build_flags` block in **both** `platformio.ini` and `platformio.ini.sample`:

```ini
build_flags =
    -D VERSION_MAJOR=1
    -D VERSION_MINOR=6
    -D VERSION_PATCH=0
    -D AP_SSID_PREFIX=\"esp-setup\"
    -D AP_PASSWORD=\"changeme123\"
```

Every credential flag is deleted. `AP_PASSWORD` is the only remaining secret and grants nothing beyond a provisioning session on a device already in AP mode.

- [ ] **Step 3: Verify no secrets remain in the binary**

```bash
pio run -e nodemcuv2
strings .pio/build/nodemcuv2/firmware.bin | grep -nE 'PSK|hunter|BattleField|hivemq|heq9' || echo "CLEAN: no known credentials"
grep -cE 'WIFI_PASSWORD|MQTT_PASSWORD|UPDATE_PASSWORD' platformio.ini || echo "CLEAN: no credential flags"
```

Expected: both print their CLEAN message. This is the acceptance criterion for the whole plan.

- [ ] **Step 4: Run the full test suite**

Run: `pio test -e native`
Expected: PASS, 21 tests.

- [ ] **Step 5: Delete backup/ and update the README**

```bash
git rm -r backup/
```

Rewrite the README's Installation and Web Updater sections to describe first-boot provisioning: flash, join `esp-setup-XXXXXX` with the AP password, open `http://192.168.4.1`, enter WiFi and MQTT details, save. Document the three recovery paths and note that the FLASH button must be pressed **after** power-on, not held during it, since holding GPIO0 low at reset enters the ROM bootloader instead.

- [ ] **Step 6: Run the hardware matrix**

Work through the table in the spec's Testing section. All ten rows must pass, in particular:
- Filesystem update uploaded — config survives (this is why config lives in EEPROM, not LittleFS).
- Valid config, wrong PSK — falls back to AP after 30s and retries every 5 minutes.

- [ ] **Step 7: Commit**

```bash
git add src/main.cpp platformio.ini.sample README.md
git commit -m "feat: complete AP provisioning cutover to v1.6.0

Removes every credential build flag; published binaries no longer
contain WiFi, MQTT or updater secrets. main.cpp is now wiring only.
Adds ADC_MODE(ADC_VCC) so /info reports a real supply voltage, and
drops the duplicate mDNS and unused wpa2_enterprise includes."
```

---

## Plan Self-Review

**Spec coverage.** Every spec section maps to a task: module layout (Tasks 2, 4-8, 10), boot state machine (5), captive portal (5), config schema (1), HTTP interface (7, 8), password redaction (8), MQTT commands and LWT (6), three recovery paths (5 for GPIO0, 6 for MQTT, 9 for WebUI), build-flag migration (10), all twelve defects (3, 4, 6, 7, 9, 10), cleanup (10), native tests (1, 3), hardware matrix (10).

**Defect tracking.** WOL broadcast → Task 6. Magic-byte detection → Tasks 3, 7. `upload_btn` → Task 9. `ADC_MODE` → Task 10. PubSubClient buffer → Task 6. TLS buffers → Task 6. Duplicate mDNS include → Task 10. Path traversal → Task 7. `delay()` chains → Task 6. Integer division → Task 6. CSRF/Origin → Task 7. `wpa2_enterprise` include → Task 10.

**Type consistency.** `Config`, `config()`, `config_store_save()`, `NetState`, `net_is_ap()`, `net_set_ap_forced()`, `net_factory_reset_and_reboot()`, `image_detect()`, `broadcast_addr()`, `web_require_auth()`, `guard_post()`, `mqtt_publish_status()` and `PulseStatusFn` are each defined once and used with matching signatures throughout.

**Known ordering constraints.**
1. `config_store_begin()` must precede `net_begin()`.
2. `http_server.collectHeaders("Origin")` must precede `http_server.begin()`.
3. `Update.begin()` must run on the first WRITE chunk, not on FILE_START, because the magic byte is unavailable until data arrives.
4. Task 1's `platformio.ini` change must be mirrored into `platformio.ini.sample`, since `platformio.ini` is gitignored.
