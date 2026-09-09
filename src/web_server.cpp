#include "web_server.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <Updater.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config_store.h"
#include "image_detect.h"
#include "net_manager.h"
#include "version.h"

static ESP8266WebServer http_server(80);

// update_authorized is set once per upload, at UPLOAD_FILE_START, from the
// silent post_authorized() check, so it gates every later WRITE/END chunk
// without re-checking (or re-responding) per chunk.
static bool update_started    = false;
static bool update_authorized = false;
// Latches a failed Update.begin(). Without it the next WRITE chunk would
// re-enter the detection branch and run image_detect() on mid-file bytes,
// which are essentially never 0xE9, so it would pick U_FS and write the tail
// of a firmware image over LittleFS — then report success.
static bool update_failed     = false;

bool web_require_auth() {
    const char* user = config_store_is_valid() ? config().upd_user : "admin";
    const char* pass = config_store_is_valid() ? config().upd_pass : "admin";
    if (!http_server.authenticate(user, pass)) {
        http_server.requestAuthentication();
        return false;
    }
    return true;
}

// Rejects cross-site POSTs. Same-origin requests either omit Origin or send
// one matching this device. Missing Origin is allowed on purpose: browsers
// always send it on cross-site POSTs, so its absence isn't the attack path,
// and requiring it would break curl and the failsafe HTML form.
static bool same_origin() {
    if (!http_server.hasHeader("Origin")) return true;   // curl, non-browser
    const String host = http_server.hostHeader();
    if (host.length() == 0) return false;                // no Host: cannot verify
    return http_server.header("Origin") == String(F("http://")) + host;
}

// Same checks as guard_post(), but silent: the upload callback runs while the
// request body is still being read, so the response belongs to the main handler.
static bool post_authorized() {
    const char* user = config_store_is_valid() ? config().upd_user : "admin";
    const char* pass = config_store_is_valid() ? config().upd_pass : "admin";
    return http_server.authenticate(user, pass) && same_origin();
}

static bool guard_post() {
    if (!web_require_auth()) return false;
    if (!same_origin()) {
        http_server.send(403, "text/plain", "Cross-origin POST rejected");
        return false;
    }
    return true;
}

// The distinction between these two is the whole password-redaction contract:
// a secret arriving empty means "keep what is stored", so the browser never
// has to hold or resend it.
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

static size_t fs_size_for_update() {
    extern uint32_t _FS_start;
    extern uint32_t _FS_end;
    return (size_t)&_FS_end - (size_t)&_FS_start;
}

void web_begin()
{
    // Must run before http_server.begin() — ESP8266WebServer does not retain
    // arbitrary headers otherwise, and hasHeader("Origin") would always
    // return false, silently disabling the CSRF guard below.
    http_server.collectHeaders("Origin");

    // mDNS is NOT started here. web_begin() runs immediately after net_begin(),
    // which only *starts* the station association — there is no IP yet, and a
    // responder bound at that moment never answers. net_manager calls
    // web_start_mdns() on the transition to NET_STA_CONNECTED instead.

    // Serve the HTML page from LittleFS
    http_server.on("/", HTTP_GET, []()
                   {
        if (!web_require_auth()) return;
        File file = LittleFS.open("/index.html", "r");
        if (!file) {
            // Failsafe HTML. PROGMEM + send_P keeps it in flash: a plain string
            // literal on the ESP8266 is copied into DRAM at startup, and this
            // page is ~1.6 KB that is only ever needed when LittleFS is empty.
            static const char failsafe_html[] PROGMEM =
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
                "<label>Updater Username</label><input id=n "
                "placeholder='blank = keep current'>"
                "<label>Updater Password</label><input id=w type=password "
                "placeholder='blank = keep current'>"
                "<p><small>The updater login is <b>admin</b>/<b>admin</b> until you "
                "change it here. Do that before leaving the setup AP.</small></p>"
                "<button onclick=\"save()\">Save &amp; Reboot</button>"
                "<hr><h3>Upload Firmware or Filesystem</h3>"
                "<form method='POST' action='/update' enctype='multipart/form-data'>"
                "<input type='file' name='update' accept='.bin'>"
                "<button type='submit'>Upload</button></form>"
                "<hr><h3>Recovery</h3>"
                "<p><small>Erases the stored configuration and reboots into the "
                "setup AP.</small></p>"
                "<button onclick=\"fr()\">Factory Reset</button>"
                "<script>function save(){"
                "var b={wifi_ssid:s.value,wifi_psk:p.value,mqtt_host:h.value,"
                "mqtt_port:parseInt(o.value||'8883'),mqtt_user:u.value,"
                "mqtt_pass:m.value,device_id:d.value};"
                // upd_user goes through copy_field, which would happily store an
                // empty string, so omit the key when the box is blank; upd_pass
                // goes through copy_secret, which already treats empty as keep.
                "if(n.value)b.upd_user=n.value;if(w.value)b.upd_pass=w.value;"
                "fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},"
                "body:JSON.stringify(b)}).then(r=>r.json()).then(j=>{"
                "document.body.innerHTML=j.ok?'<h2>Saved. Rebooting...</h2>':"
                "'<h2>Error: '+(j.error||'unknown')+'</h2>';});}"
                "function fr(){if(!confirm('Erase all settings and reboot?'))return;"
                "fetch('/factory_reset',{method:'POST',"
                "headers:{'Content-Type':'application/json'},body:'{}'});"
                "document.body.innerHTML='<h2>Factory reset. Rebooting...</h2>';}</script>"
                "</body></html>";
            http_server.send_P(200, PSTR("text/html"), failsafe_html);
            return;
        }
        http_server.streamFile(file, "text/html");
        file.close(); });

    http_server.on("/index.js", HTTP_GET, []()
                   {
        if (!web_require_auth()) return;
        File file = LittleFS.open("/index.js", "r");
        if (!file) {
            http_server.send(500, "text/plain", "index.js file missing. Please upload filesystem.");
            return;
        }
        http_server.streamFile(file, "text/javascript");
        file.close(); });

    http_server.on("/styles.css", HTTP_GET, []()
                   {
        if (!web_require_auth()) return;
        File file = LittleFS.open("/styles.css", "r");
        if (!file) {
            http_server.send(500, "text/plain", "styles.css file missing. Please upload filesystem.");
            return;
        }
        http_server.streamFile(file, "text/css");
        file.close(); });

    // Handle reboot
    http_server.on("/reboot", HTTP_POST, []()
                   {
        if (!guard_post()) return;
        http_server.send(200, "text/plain");
        delay(1000);
        ESP.restart(); });

    http_server.on("/config", HTTP_GET, []()
                   {
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
        http_server.send(200, "application/json", out); });

    http_server.on("/config", HTTP_POST, []()
                   {
        if (!guard_post()) return;
        JsonDocument doc;
        if (deserializeJson(doc, http_server.arg("plain"))) {
            http_server.send(400, "application/json", "{\"error\":\"malformed json\"}");
            return;
        }
        Config tmp = config();          // plain struct copy is safe: config_set_defaults and
                                         // config_deserialize both memset, so padding is zeroed
        copy_field (tmp.wifi_ssid, sizeof(tmp.wifi_ssid), doc["wifi_ssid"]);
        copy_secret(tmp.wifi_psk,  sizeof(tmp.wifi_psk),  doc["wifi_psk"]);
        copy_field (tmp.mqtt_host, sizeof(tmp.mqtt_host), doc["mqtt_host"]);
        copy_field (tmp.mqtt_user, sizeof(tmp.mqtt_user), doc["mqtt_user"]);
        copy_secret(tmp.mqtt_pass, sizeof(tmp.mqtt_pass), doc["mqtt_pass"]);
        copy_field (tmp.device_id, sizeof(tmp.device_id), doc["device_id"]);
        copy_field (tmp.mdns_host, sizeof(tmp.mdns_host), doc["mdns_host"]);
        copy_field (tmp.upd_user,  sizeof(tmp.upd_user),  doc["upd_user"]);
        copy_secret(tmp.upd_pass,  sizeof(tmp.upd_pass),  doc["upd_pass"]);
        if (doc["mqtt_port"].is<unsigned short>()) tmp.mqtt_port = doc["mqtt_port"];

        if (tmp.wifi_ssid[0] == '\0') {
            http_server.send(400, "application/json", "{\"error\":\"wifi_ssid required\"}");
            return;                     // live config untouched
        }

        // Adopts tmp as the live config only after the EEPROM commit succeeds,
        // so no rollback snapshot is needed: on failure the live config is
        // still byte-identical to what is persisted.
        if (!config_store_save_from(tmp)) {
            http_server.send(500, "application/json", "{\"error\":\"eeprom write failed\"}");
            return;
        }
        http_server.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
        delay(500);
        ESP.restart(); });

    http_server.on("/ap_mode", HTTP_POST, []()
                   {
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

    http_server.on("/factory_reset", HTTP_POST, []()
                   {
        if (!guard_post()) return;
        http_server.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
        delay(500);
        net_factory_reset_and_reboot(); });

    http_server.on("/info", HTTP_GET, []()
                   {
        if (!web_require_auth()) return;

        JsonDocument esp_info_doc;
        String esp_info;

        // Add firmware version so the UI can display it
        esp_info_doc["firmware_version"] = "v" + firmware_version;
        esp_info_doc["boot_mode"] = ESP.getBootMode();
        esp_info_doc["boot_version"] = ESP.getBootVersion();
        esp_info_doc["chip_id"] = ESP.getChipId();
        esp_info_doc["core_version"] = ESP.getCoreVersion();
        esp_info_doc["cpu_freq"] = ESP.getCpuFreqMHz();
        esp_info_doc["cycle_count"] = ESP.getCycleCount();
        esp_info_doc["flash_chip_id"] = ESP.getFlashChipId();
        esp_info_doc["flash_chip_mode"] = ESP.getFlashChipMode();
        esp_info_doc["flash_chip_size"] = ESP.getFlashChipSize();
        esp_info_doc["flash_chip_size_by_chip_id"] = ESP.getFlashChipSizeByChipId();
        esp_info_doc["flash_chip_speed"] = ESP.getFlashChipSpeed();
        esp_info_doc["flash_chip_vendor_id"] = ESP.getFlashChipVendorId();
        esp_info_doc["free_continuous_stack"] = ESP.getFreeContStack();
        esp_info_doc["free_heap"] = ESP.getFreeHeap();
        esp_info_doc["free_sketch_space"] = ESP.getFreeSketchSpace();
        esp_info_doc["esp_full_version"] = ESP.getFullVersion();
        esp_info_doc["heap_fragmentation"] = ESP.getHeapFragmentation();
        esp_info_doc["max_free_block_size"] = ESP.getMaxFreeBlockSize();
        esp_info_doc["reset_info"] = ESP.getResetInfo();
        esp_info_doc["reset_reason"] = ESP.getResetReason();
        esp_info_doc["sdk_version"] = ESP.getSdkVersion();
        esp_info_doc["sketch_md5"] = ESP.getSketchMD5();
        esp_info_doc["sketch_size"] = ESP.getSketchSize();
        esp_info_doc["vcc"]= ESP.getVcc();

        serializeJson(esp_info_doc, esp_info);
        http_server.send(200, "application/json", esp_info.c_str()); });

    // Handle the upload
    http_server.on("/update", HTTP_POST, []()
                   {
        if (!guard_post()) return;
        http_server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        delay(1000);
        ESP.restart(); }, []()
                   {
        HTTPUpload& upload = http_server.upload();

        if (upload.status == UPLOAD_FILE_START) {
            update_started    = false;
            update_failed     = false;
            update_authorized = post_authorized();
            Serial.printf("Update: %s\n", upload.filename.c_str());
        }
        if (!update_authorized || update_failed) return;

        if (upload.status == UPLOAD_FILE_WRITE) {
            if (!update_started) {
                const ImageType type = image_detect(upload.buf, upload.currentSize);
                const int    command = (type == IMAGE_FIRMWARE) ? U_FLASH : U_FS;
                const size_t size    = (type == IMAGE_FIRMWARE)
                                     ? ((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)
                                     : fs_size_for_update();
                Serial.printf("Target: %s\n", type == IMAGE_FIRMWARE ? "Firmware" : "Filesystem");
                if (!Update.begin(size, command)) {
                    Update.printError(Serial);
                    update_failed = true;   // never re-detect on mid-file bytes
                    return;
                }
                update_started = true;
            }
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) Serial.printf("Success: %u bytes\n", upload.totalSize);
            else                  Update.printError(Serial);
        } });

    // Handle 404 and Static Files (JS/CSS)
    http_server.onNotFound([]()
                           {
        // Captive portal. The DNS server already points every name at the
        // softAP address, but a portal probe (captive.apple.com/hotspot-detect,
        // /generate_204, ...) still lands on an unknown path. Without this it
        // would get a Basic-auth prompt and then a 404 instead of the setup
        // page. Deliberately ahead of the auth check: a 302 to the device's
        // own root discloses nothing that the SSID has not already disclosed.
        // In AP mode every real asset has an explicit route, so nothing that
        // matters is lost by redirecting the rest.
        if (net_is_ap()) {
            http_server.sendHeader("Location",
                                   String("http://") + WiFi.softAPIP().toString() + "/");
            http_server.send(302, "text/plain", "");
            return;
        }

        if (!web_require_auth()) return;

        String path = http_server.uri();
        if (path.indexOf("..") >= 0) {
            http_server.send(400, "text/plain", "Bad path");
            return;
        }

        if (LittleFS.exists(path)) {
            File file = LittleFS.open(path, "r");
            String contentType = "text/plain";
            if (path.endsWith(".html")) contentType = "text/html";
            else if (path.endsWith(".css")) contentType = "text/css";
            else if (path.endsWith(".js")) contentType = "application/javascript";
            else if (path.endsWith(".ico")) contentType = "image/x-icon";

            http_server.streamFile(file, contentType);
            file.close();
            return;
        }

        // If file not found, serve custom 404
        File file = LittleFS.open("/404.html", "r");
        if (file) {
             http_server.streamFile(file, "text/html");
             file.close();
        } else {
             http_server.send(404, "text/plain", "404 Not Found (and 404 file missing)");
        } });

    http_server.begin();


}

void web_loop()
{
    http_server.handleClient();
    MDNS.update();
}

void web_on_network_up()
{
    // Re-bind the listener. web_begin() already called begin(), but that runs
    // before DHCP has completed on a station boot, and a listener bound with no
    // interface address does not accept once the address arrives. Re-binding
    // here is what makes the UI reachable in STA mode; in AP mode the softAP
    // address already existed at web_begin() time, which is why only STA broke.
    http_server.stop();
    http_server.begin();

    MDNS.end();                       // no-op if never started; safe on reconnect
    if (MDNS.begin(config().mdns_host)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("HTTP + mDNS up: http://%s.local  http://%s\n",
                      config().mdns_host, WiFi.localIP().toString().c_str());
    } else {
        Serial.printf("HTTP up on http://%s (mDNS responder failed)\n",
                      WiFi.localIP().toString().c_str());
    }
}
