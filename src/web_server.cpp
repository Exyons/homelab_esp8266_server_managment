#include "web_server.h"
#include <Arduino.h>
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

    MDNS.begin(config().mdns_host);

    // Serve the HTML page from LittleFS
    http_server.on("/", HTTP_GET, []()
                   {
        if (!web_require_auth()) return;
        File file = LittleFS.open("/index.html", "r");
        if (!file) {
            // Failsafe HTML
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
            http_server.send(200, "text/html", failsafe_html);
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
            update_authorized = post_authorized();
            Serial.printf("Update: %s\n", upload.filename.c_str());
        }
        if (!update_authorized) return;

        if (upload.status == UPLOAD_FILE_WRITE) {
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
        } });

    // Handle 404 and Static Files (JS/CSS)
    http_server.onNotFound([]()
                           {
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

    MDNS.addService("http", "tcp", 80);
}

void web_loop()
{
    http_server.handleClient();
    MDNS.update();
}
