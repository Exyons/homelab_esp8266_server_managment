# AP-Mode Provisioning & Runtime Configuration

**Date:** 2026-09-08
**Target version:** 1.6.0 (from 1.5.2)
**Status:** Approved for implementation

## Problem

Every credential this firmware uses is a compile-time `-D` build flag. The
Arduino toolchain embeds each one in the binary as a plaintext string, so
`strings firmware.bin` recovers the WiFi PSK, the MQTT credentials and the
web-updater password from any build.

This is not theoretical. 49 such binaries were committed to a public GitHub
repository and published as release assets between 2025-12-18 and 2026-01-01.
Git history and the release assets have since been purged and the credentials
rotated, but the underlying defect remains: the next published binary leaks the
next set of credentials in exactly the same way.

The fix is to remove credentials from the binary entirely and supply them at
runtime, which requires a provisioning path that works on a device with no
network configuration.

## Goals

1. A firmware binary contains no WiFi, MQTT, or updater credentials.
2. A device with no configuration is provisionable over WiFi, with no serial
   cable and no reflash.
3. A device whose configuration has gone stale (router replaced, PSK changed)
   recovers without physical access wherever possible, and always recovers with
   physical access to the FLASH button.
4. Configuration survives both firmware and filesystem updates.

## Non-Goals

- Encryption of credentials at rest. The ESP8266 has no secure element. Anyone
  with physical access and `esptool` can dump flash and read the config. The
  goal is clean *published binaries*, not a tamper-proof device.
- WPA2-Enterprise support. That work lives on the `wpa2_enterprise` branch and
  is out of scope here.
- Multi-network roaming or credential lists. One network, one broker.

## Architecture

### Module layout

PlatformIO compiles everything under `src/` automatically; no build
configuration change is needed.

| Header | Source | Approx. lines | Responsibility |
|---|---|---|---|
| `include/config_store.h` | `src/config_store.cpp` | 160 | EEPROM struct, CRC32, load/save/reset |
| `include/net_manager.h` | `src/net_manager.cpp` | 180 | AP/STA state machine, captive portal |
| `include/web_server.h` | `src/web_server.cpp` | 280 | HTTP routes, failsafe HTML |
| `include/mqtt_client.h` | `src/mqtt_client.cpp` | 170 | Broker connection, LWT, command dispatch |
| `include/pulse_action.h` | `src/pulse_action.cpp` | 50 | Relay pulse timers (lifted unchanged) |
| — | `src/main.cpp` | 90 | `setup()` / `loop()` wiring only |

Each module exposes `begin()` and, where it owns recurring work, `loop()`.
`main.cpp` calls them in order and holds no logic of its own.

### Boot state machine

Owned by `net_manager`. States: `NET_BOOT`, `NET_STA_CONNECTING`,
`NET_STA_CONNECTED`, `NET_AP`.

```
setup()
  |
  +-- GPIO0 (FLASH) held LOW for 5s? --> wipe EEPROM ---+
  |                                                      |
  +-- config invalid (bad magic or CRC)? ---------------+
  |                                                      |
  +-- config.ap_forced set? ----------------------------+
  |                                                      v
  +-- else: NET_STA_CONNECTING (30s, non-blocking)   NET_AP
            |                                         - SSID esp-setup-<chipid>
            +-- connected --> NET_STA_CONNECTED       - WPA2 via AP_PASSWORD
            |                 start mDNS + MQTT       - gateway 192.168.4.1
            |                                         - captive-portal DNS
            +-- 30s timeout -------------------> NET_AP
                                                      |
                        every 5 min while in NET_AP: if config is valid
                        and ap_forced is false, retry STA; on success
                        reboot into NET_STA_CONNECTED
```

The existing `setup_wifi()` blocks forever when WiFi is unavailable, which
prevents the web server from ever starting. The state machine removes that
failure mode by construction: the HTTP server starts before WiFi is resolved.

The 5-minute retry makes the device self-healing. A router reboot or a
transient outage returns it to STA on its own; only an explicit `ap_forced`
keeps it in AP indefinitely.

### Captive portal

While in `NET_AP`, a core `DNSServer` answers every domain with 192.168.4.1.
Phones and laptops detect this and open the configuration page automatically
rather than requiring the user to type an IP address. Costs roughly 50 lines
and a small amount of RAM.

## Configuration store

```c
#define CONFIG_MAGIC   0xC0FFEE01
#define CONFIG_VERSION 1

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
```

- Fields sum to 438 bytes, but `sizeof(Config)` is 440 on the ESP8266's 32-bit
  ABI: alignment inserts one pad byte before `mqtt_port` and one before
  `crc32`. `EEPROM.begin(512)` covers it.
- **Padding must be zeroed.** `memset(&cfg, 0, sizeof(cfg))` before populating,
  on every save. Uninitialized pad bytes would otherwise make the CRC
  non-deterministic and configs would fail validation at random.
- `crc32` must remain the final member. It is computed over exactly
  `offsetof(Config, crc32)` bytes from the start of the struct, never over
  `sizeof(Config)`, so the field itself and its preceding pad byte are excluded.
- A config is valid only when `magic == CONFIG_MAGIC` and the stored CRC
  matches the recomputed CRC. Any other state is treated as unconfigured and
  routes the device to AP mode.
- `version` exists so that adding a field later can migrate existing devices
  rather than forcing a factory reset.
- `mqtt_port` replaces the hardcoded `8883`.

The ESP8266 EEPROM library writes to a dedicated flash sector that is part of
neither the sketch region nor the LittleFS region. Configuration therefore
survives both a firmware update and a filesystem update. This is the specific
reason EEPROM was chosen over a JSON file on LittleFS, which the device's own
filesystem updater would erase on every upload.

## HTTP interface

| Route | Method | Purpose |
|---|---|---|
| `/config` | GET | Current configuration, passwords redacted |
| `/config` | POST | Validate, persist, reboot |
| `/ap_mode` | POST | Set or clear `ap_forced`, reboot |
| `/factory_reset` | POST | Wipe EEPROM, reboot |
| `/` | GET | Updater UI (existing) |
| `/info` | GET | Device info JSON (existing) |
| `/update` | POST | Firmware/filesystem upload (existing) |
| `/reboot` | POST | Restart (existing) |

### Credential handling

`GET /config` never returns a stored password. Each secret field is returned as
an empty string alongside a boolean presence flag:

```json
{
  "wifi_ssid": "MyNetwork",
  "wifi_psk": "",
  "has_wifi_psk": true,
  "mqtt_host": "broker.example.com",
  "mqtt_port": 8883,
  "mqtt_pass": "",
  "has_mqtt_pass": true,
  "ap_forced": false
}
```

On `POST /config`, an empty password field means "keep the stored value". This
lets the user change the MQTT host without retyping every secret, and prevents
secrets from being echoed into browser history, caches, or the DOM.

### Authentication

All routes require HTTP Basic auth against `config.upd_user` / `config.upd_pass`.
When the config is invalid, authentication falls back to `admin` / `admin` and
the UI shows a persistent banner instructing the user to change both. The
banner clears once the stored credentials differ from the defaults.

### Failsafe HTML

`src/web_server.cpp` already contains a compiled-in failsafe page for when
`index.html` is missing from LittleFS. That page must gain a provisioning form.

This matters because a freshly flashed device has both an empty EEPROM and an
empty filesystem. Without a compiled-in form, such a device reaches AP mode with
no interface to configure it from. The failsafe path is the one route that must
never depend on the filesystem.

## MQTT

New commands, dispatched through the existing `callback()` chain:

| Command | Effect |
|---|---|
| `AP_MODE_ON` | Set `ap_forced`, reboot into AP |
| `AP_MODE_OFF` | Clear `ap_forced`, reboot into STA |
| `FACTORY_RESET` | Wipe EEPROM, reboot into AP |
| `WIFI_STATUS` | Publish current state, SSID, RSSI, IP |

Also added: a Last Will and Testament so the status topic reports `offline`
when the device drops, and retained status publishes so a late subscriber sees
current state rather than silence.

## Recovery paths

Three independent ways to force AP mode or reset, chosen so that no single
failure locks the user out:

1. **WebUI toggle** — requires the device to be reachable on the LAN.
2. **MQTT command** — requires a working broker connection.
3. **GPIO0 FLASH button**, held 5 seconds at boot with LED feedback — requires
   physical access and works when WiFi, MQTT and HTTP are all unreachable.

## Build flag migration

All credential flags are removed. The binary retains only:

```ini
build_flags =
    -D VERSION_MAJOR=1
    -D VERSION_MINOR=6
    -D VERSION_PATCH=0
    -D AP_SSID_PREFIX=\"esp-setup\"
    -D AP_PASSWORD=\"changeme123\"
```

`AP_PASSWORD` is the single remaining secret in the binary. It grants only a
provisioning session on a device already in AP mode, not access to any network
or broker. It is changeable from the UI, at which point the compiled default
becomes a fallback used only after a factory reset.

`platformio.ini.sample` is updated to match, and the README documents the new
first-boot provisioning flow.

## Defects fixed in the same pass

Carried from the 2026-09-08 code analysis:

| Location | Defect | Fix |
|---|---|---|
| `send_magic_packet` | `beginPacketMulticast` used for a directed broadcast; broadcast IP hardcoded to `10.10.10.255` | `beginPacket`, address derived as `WiFi.localIP() \| ~WiFi.subnetMask()` |
| `/update` handler | Firmware vs filesystem chosen by filename substring; a renamed file flashes an FS image into the sketch slot and bricks the device | Detect by first byte: `0xE9` means firmware, otherwise filesystem |
| `data/index.js:66` | `upload_btn.textContent` — id is `upload-btn`, which is not a JS global; throws `ReferenceError` when the file selection is cleared | Use `upload_btn_text` |
| `/info` | `ESP.getVcc()` returns garbage without `ADC_MODE(ADC_VCC)` | Add the macro; A0 is unused in this project |
| MQTT setup | PubSubClient default 256-byte buffer silently drops longer publishes | `client.setBufferSize(512)` |
| TLS setup | `WiFiClientSecure` default buffers are ~16KB each, causing heap exhaustion alongside the web server | `setBufferSizes(1024, 1024)` |
| `main.cpp:2,7` | `ESP8266mDNS.h` included twice | Remove the duplicate |
| `onNotFound` | `LittleFS.exists()` on an unsanitized request path | Reject paths containing `..` |
| `callback()` | `delay(100)` chains stall `client.loop()` and MQTT keepalive | Remove |
| `reconnect()` | `mqtt_reconnect_interval / 1000` is integer division before the cast to double | Compute in floating point |
| State-changing POSTs | Basic auth is browser-cached, so any page can POST to `/update` or `/reboot` | Check the `Origin` header |
| `main.cpp:11` | Unused `wpa2_enterprise.h` include | Remove from `main`; it belongs on the `wpa2_enterprise` branch |

## Cleanup

- Delete `backup/` (7 stale `.cpp` snapshots superseded by git history).
- Update `README.md` for the provisioning flow and the removal of credential
  build flags.

## Testing

### Native unit tests (`pio test -e native`, no hardware)

Pure logic is extracted so it can be tested off-device:

- CRC32 round-trip over a populated struct.
- Config validation rejects blank EEPROM, wrong magic, and corrupted CRC.
- Firmware/filesystem detection by magic byte, including the `0xE9` boundary.
- Broadcast address computation across several IP/netmask pairs.

### Manual hardware matrix

| Scenario | Expected |
|---|---|
| Fresh flash, empty EEPROM | Boots to AP, captive portal opens, provisioning succeeds |
| Valid config, router present | Connects to STA within 30s, MQTT online |
| Valid config, wrong PSK | Falls back to AP after 30s, retries STA every 5 min |
| Router rebooted mid-run | Reconnects without intervention |
| Filesystem update uploaded | Config survives; device returns to STA |
| Firmware update uploaded | Config survives; device returns to STA |
| WebUI AP toggle | Reboots to AP and stays until toggled off |
| MQTT `AP_MODE_ON` | Same |
| FLASH button held 5s at boot | EEPROM wiped, boots to AP |
| `strings firmware.bin` | No WiFi PSK, no MQTT credentials, no updater password |

## Consequences

- Flashing 1.6.0 to an existing device finds an empty EEPROM and boots to AP
  once. The user provisions it a single time. This is expected and must be
  stated in the release notes.
- Published release binaries become safe to attach to GitHub Releases, which
  was the original motivation.
- Sketch size grows by roughly 40KB. The `eagle.flash.4m1m.ld` layout leaves
  approximately 1MB for the sketch against a current 485KB, so OTA headroom
  remains adequate. Verify actual size after the first build.
