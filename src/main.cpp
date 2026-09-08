#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Updater.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "pulse_action.h"
#include "config_store.h"
#include "net_manager.h"
#include "mqtt_client.h"

// #define STRINGIFY(x) #x
// #define TOSTRING(x) STRINGIFY(x)
// const char *firmware_version = TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_MINOR) "." TOSTRING(VERSION_PATCH);
String firmware_version = String(VERSION_MAJOR) + "." + String(VERSION_MINOR) + "." + String(VERSION_PATCH);

// Web updater setup
String mdns_hostname = MDNS_HOSTNAME;
const char *update_username = UPDATE_USERNAME;
const char *update_password = UPDATE_PASSWORD;

// Pin definitions
const int POWER_PIN_WIN_SERVER = 14; // GPIO14 (D5 on board)
const int POWER_PIN_NAS_SERVER = 5;  // GPIO5  (D1 on board)

ESP8266WebServer http_server(80);

PulseAction win_server;
PulseAction nas_server;

void setup_webupdater()
{
    MDNS.begin(mdns_hostname);

    // Serve the HTML page from LittleFS
    http_server.on("/", HTTP_GET, []()
                   {
        if (!http_server.authenticate(update_username, update_password)) {
            return http_server.requestAuthentication();
        }
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
        if (!http_server.authenticate(update_username, update_password)) {
            return http_server.requestAuthentication();
        }
        File file = LittleFS.open("/index.js", "r");
        if (!file) {
            http_server.send(500, "text/plain", "index.js file missing. Please upload filesystem.");
            return;
        }
        http_server.streamFile(file, "text/javascript");
        file.close(); });

    http_server.on("/styles.css", HTTP_GET, []()
                   {
        if (!http_server.authenticate(update_username, update_password)) {
            return http_server.requestAuthentication();
        }
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
        if(!http_server.authenticate(update_username, update_password)){
            return http_server.requestAuthentication();
        } 
        http_server.send(200, "text/plain");
        delay(1000);
        ESP.restart(); });

    http_server.on("/info", HTTP_GET, []()
                   {
        if(!http_server.authenticate(update_username, update_password)){
            return http_server.requestAuthentication();
        }
        
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
        if (!http_server.authenticate(update_username, update_password)) {
            return http_server.requestAuthentication();
        }
        http_server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        delay(1000);
        ESP.restart(); }, []()
                   {
        if (!http_server.authenticate(update_username, update_password)) {
            return;
        }
        HTTPUpload& upload = http_server.upload();
    
        if (upload.status == UPLOAD_FILE_START) {
            Serial.printf("Update: %s\n", upload.filename.c_str());
            
            // Determine if this is a Firmware (Sketch) or Filesystem (LittleFS) update
            int command = U_FLASH;
            size_t updateSize = 0;

            if (upload.filename.indexOf("nigga_filesystem") > -1 || upload.filename.indexOf("nigga_spiffs") > -1) {
                command = U_FS;
                Serial.println("Target: Filesystem");
                // Get the actual size of the FS partition from linker symbols
                extern uint32_t _FS_start;
                extern uint32_t _FS_end;
                updateSize = (size_t)&_FS_end - (size_t)&_FS_start;
            } else {
                command = U_FLASH;
                Serial.println("Target: Firmware");
                updateSize = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            }

            // Start the update
            if (!Update.begin(updateSize, command)) { 
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) { 
                Serial.printf("Success: %u bytes\n", upload.totalSize);
            } else {
                Update.printError(Serial);
            }
        } });

    // Handle 404 and Static Files (JS/CSS)
    http_server.onNotFound([]()
                           {
        if (!http_server.authenticate(update_username, update_password)) {
            return http_server.requestAuthentication();
        }

        String path = http_server.uri();
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

void setup()
{
    win_server.init(POWER_PIN_WIN_SERVER, "win-server", mqtt_publish_status);
    nas_server.init(POWER_PIN_NAS_SERVER, "nas-server", mqtt_publish_status);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    Serial.begin(115200);

    // Mount LittleFS
    if (!LittleFS.begin())
    {
        Serial.println("LittleFS mount failed");
        // Optional: format if mount fails. Be careful with this in production.
        // LittleFS.format();
    }
    else
    {
        Serial.println("LittleFS mounted successfully");
    }

    config_store_begin();
    net_begin();

    setup_webupdater();

    mqtt_begin(&win_server, &nas_server);
}

void loop()
{
    net_loop();

    http_server.handleClient();
    MDNS.update();
    mqtt_loop();
    win_server.update();
    nas_server.update();
}
