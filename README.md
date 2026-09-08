# Homelab Server Management

This project is a PlatformIO based firmware for the ESP8266 (NodeMCU v2) designed for homelab server management tasks. It utilizes the Arduino framework and includes features for MQTT communication and convenient web-based firmware updates.

WiFi, MQTT and web-updater credentials are no longer compiled into the firmware. They are entered once over a WPA2-protected setup access point and stored in EEPROM. Published binaries do not contain any of these secrets.

## Features

* **Platform:** ESP8266 (NodeMCU v2)
* **Connectivity:** WiFi & mDNS support
* **Provisioning:** Captive-portal setup over a WPA2 access point on first boot; no credentials baked into the binary.
* **MQTT:** Integrated `PubSubClient` for MQTT communication.
* **Web Updater:** A custom, user-friendly web interface for Over-The-Air (OTA) firmware updates.
* **Authentication:** Web updater is protected by a username/password set during provisioning.
* **UI:** Modern, responsive dark/light mode interface.
* **Progress Tracking:** Real-time upload progress bar.

## Getting Started

### Prerequisites

* [PlatformIO](https://platformio.org/) (VSCode Extension or CLI)

### Installation

1. Clone the repository.
2. Open the project in PlatformIO.
3. Copy `platformio.ini.sample` to `platformio.ini`. The sample only sets the firmware version, the setup AP's SSID prefix (`AP_SSID_PREFIX`) and its password (`AP_PASSWORD`) — no WiFi, MQTT or updater credentials are build flags any more. Change `AP_PASSWORD` from the default (`changeme123`) before building; it is the one remaining build-time secret, and it only grants access to a provisioning session on a device that is already in AP mode.
4. Build and upload **both** targets to your ESP8266 device — these are separate PlatformIO steps and both matter:
   - `pio run -e nodemcuv2 -t upload` — the firmware.
   - `pio run -e nodemcuv2 -t uploadfs` — the filesystem image, which contains the web UI (`data/`), including the full settings panel.

   If you skip the filesystem upload, the device still boots and serves a page, but it cannot read `index.html` from LittleFS and falls back to a small built-in setup form instead. That fallback form has fields for WiFi SSID/password, MQTT host/port/user/pass, Device ID and the web updater username/password, plus a factory-reset button — but **no field for the mDNS hostname**, so that stays at its compiled-in default (`esp-updater`) until you upload the filesystem image later and reach the full settings panel.
5. On first boot the device has no WiFi or MQTT configuration, so it starts its own access point named `esp-setup-XXXXXX` (the suffix is the device's chip ID). From a phone or laptop, join that network using the `AP_PASSWORD` you set above.
6. Open `http://192.168.4.1` in a browser. Most devices redirect there automatically (captive portal); if not, navigate to it manually. The device asks for an HTTP login before showing anything — until you set your own web updater username and password, this is `admin` / `admin`. Log in with that.
7. Fill in your WiFi SSID and password and your MQTT broker host/port/credentials, then save:
   - **If you uploaded the filesystem image** (recommended): the full page loads. Click the gear icon to open the settings panel — it also has fields for the mDNS hostname and a new web updater username/password. Set those now and change the login from `admin`/`admin` while you're there; the panel shows a warning banner as a reminder if you don't.
   - **If you did not upload the filesystem image**: you get the reduced fallback form described above. Fill in what it offers and save — including the updater username and password, which you should change from `admin`/`admin` here before you leave the setup AP. Leave either of those two boxes blank to keep the current value. The mDNS hostname stays at its default (`esp-updater`) until you upload the filesystem image and revisit the full settings panel.
8. The device reboots, joins your WiFi network, and the setup AP is torn down. From then on it is reachable on your LAN at the address shown by your router or via mDNS.

If the device cannot join your WiFi (wrong password, network out of range, etc.) it waits 30 seconds, then falls back to the setup AP so you can fix the configuration. While configured but unreachable, it retries the station connection every 5 minutes without leaving AP mode, so it recovers on its own once the network problem clears.

### Web Updater Usage

1. Navigate to `http://<device-ip>/` or `http://<mdns-hostname>.local/`, using the hostname you set during provisioning. The updater is part of the main page; there is no separate `/update` page (`/update` accepts the upload POST only).
2. Log in with the web updater username and password you set during provisioning — or `admin`/`admin` if you have not changed it yet (see Installation above for why that might be the case, and change it from the settings panel as soon as you can).
3. Select the `.bin` firmware file generated by PlatformIO (typically found in `.pio/build/nodemcuv2/firmware.bin`).
4. Click "Update Device" to flash the new firmware.

Firmware updates do not affect the stored configuration: WiFi, MQTT and web-updater credentials live in EEPROM, not in the LittleFS filesystem image, so they survive both firmware and filesystem updates.

### Recovery and re-provisioning

If you need to change the WiFi network, MQTT broker or updater credentials later, or the device is unreachable, there are three ways back into the setup AP:

1. **WebUI toggle.** From the device's web interface, open the settings panel (gear icon) and switch on "AP mode". The device saves the setting and reboots directly into the setup AP. This control lives in the full settings panel, so it requires the filesystem image to have been uploaded (see Installation). If the filesystem is missing, the built-in fallback form has a "Factory Reset" button instead, which wipes the configuration and reboots unprovisioned into the setup AP.
2. **MQTT commands.** Publish `AP_MODE_ON` to the `osiris/esp8266/command` topic to force the device into the setup AP on the next reboot, or `AP_MODE_OFF` to let it resume normal station operation. Publish `FACTORY_RESET` to wipe the stored configuration entirely and reboot into the setup AP unprovisioned.
3. **FLASH button.** Every boot, the firmware opens a five-second window during which it watches the board's FLASH button (GPIO0). Press the button inside that window and keep holding it for a further five seconds — the on-board LED blinks while it counts down — to trigger a factory reset and reboot into the setup AP. Releasing early aborts. The console prints `Hold FLASH within 5s for factory reset...` when the window opens, so you can see exactly when to press.

   **Important:** the FLASH button must be pressed *after* power-on, during that window — not held down through power-on or reset. Holding GPIO0 low while the device comes out of reset puts the ESP8266 into its ROM bootloader instead of running the firmware, so the factory reset would never happen.

   This is the last-resort recovery: it works even when the WebUI login is unknown and the MQTT broker is unreachable. The cost is that every boot is about five seconds slower.

## Dependencies

* `knolleary/PubSubClient`

## License

[MIT](LICENSE)
