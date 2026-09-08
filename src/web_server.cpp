#include "web_server.h"
#include <Arduino.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <Updater.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config_store.h"
#include "image_detect.h"
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
                "<!DOCTYPE html><html><head><title>Failsafe Mode</title>"
                "<meta name='viewport' content='width=device-width, initial-scale=1.0'></head>"
                "<body><h1>&#9888; Failsafe Mode</h1>"
                "<p><b>Critical Error:</b> <code>index.html</code> missing.</p>"
                "<p>The filesystem appears to be broken. Use the forms below to recover.</p>"
                "<hr>"
                "<h3>Option 1: Restore Filesystem (Recommended)</h3>"
                "<p>Select the filesystem binary (must contain <code>nigga_filesystem</code> in name).</p>"
                "<form method='POST' action='/update' enctype='multipart/form-data'>"
                "<input type='file' name='update' accept='.bin'><br><br>"
                "<input type='submit' value='Upload Filesystem'>"
                "</form>"
                "<hr>"
                "<h3>Option 2: Update Firmware</h3>"
                "<form method='POST' action='/update' enctype='multipart/form-data'>"
                "<input type='file' name='update' accept='.bin'><br><br>"
                "<input type='submit' value='Upload Firmware'>"
                "</form>"
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
