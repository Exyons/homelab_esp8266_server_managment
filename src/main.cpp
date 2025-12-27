#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Updater.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// #define STRINGIFY(x) #x
// #define TOSTRING(x) STRINGIFY(x)
// const char *firmware_version = TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_MINOR) "." TOSTRING(VERSION_PATCH);
String firmware_version = String(VERSION_MAJOR) + "." + String(VERSION_MINOR) + "." + String(VERSION_PATCH);

const unsigned long mqtt_reconnect_interval = 5000;
unsigned long mqtt_reconnect_current_millis = 0;

// Web updater setup
String mdns_hostname = MDNS_HOSTNAME;
const char *update_username = UPDATE_USERNAME;
const char *update_password = UPDATE_PASSWORD;

// Update these with your network details
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

// MQTT Broker details
const char *mqtt_server = MQTT_SERVER;
const int mqtt_port = 8883;
const char *client_id = MQTT_DEVICE_ID; // Must be unique on the broker
const char *mqtt_user = MQTT_USERNAME;
const char *mqtt_password = MQTT_PASSWORD;

// Topics
const char *topic_command = "osiris/esp8266/command";
const char *topic_status = "osiris/esp8266/status";

// Pin definitions
const int POWER_PIN_WIN_SERVER = 14; // GPIO14 (D5 on board)
const int POWER_PIN_NAS_SERVER = 5;  // GPIO5  (D1 on board)

IPAddress BROADCAST_IP = IPAddress(10, 10, 10, 255);
const uint8_t SERVER_MAC[6] = {0xC8, 0xD3, 0xFF, 0x6E, 0x9E, 0xF2};

WiFiUDP udp;
WiFiClientSecure espClient;
PubSubClient client(espClient);
ESP8266WebServer http_server(80);

struct PulseAction
{
    unsigned int pin;
    unsigned long timer;
    unsigned long duration;
    bool active;
    const char *name; // Just for logging
    const char *update_message;

    // For this board HIGH means LOW and vice-versa
    void init(unsigned int _pin, const char *_name)
    {
        pin = _pin;
        name = _name;
        timer = 0;
        active = false;
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH); // Turn Off initially
    }

    void trigger(unsigned long ms, const char *init_message, const char *_update_message, const char *active_message)
    {
        if (active)
        {
            client.publish(topic_status, active_message);
            return; // Don't trigger if already running
        }

        client.publish(topic_status, init_message);

        // Serial.printf("Triggering %s for %lu ms\n", name, ms);
        digitalWrite(pin, LOW); // Turn On
        timer = millis();
        duration = ms;
        active = true;
        update_message = _update_message;
    }

    void update()
    {
        if (active && (millis() - timer) >= duration)
        {
            digitalWrite(pin, HIGH); // Turn Off
            active = false;
            client.publish(topic_status, update_message);
        }
    }
};

PulseAction win_server;
PulseAction nas_server;

const int send_magic_packet(const uint16_t port = 7)
{
    uint8_t payload_buffer[102];
    for (int i = 0; i < 6; i += 1)
    {
        payload_buffer[i] = 0xFF;
    }
    for (int i = 6; i < 102; i += 6)
    {
        for (int j = 0; j < 6; j++)
        {
            payload_buffer[i + j] = SERVER_MAC[j];
        }
    }
    udp.beginPacketMulticast(BROADCAST_IP, port, WiFi.localIP());
    udp.write(payload_buffer, sizeof(payload_buffer));
    const int status = udp.endPacket();
    return status;
}

void setup_wifi()
{
    delay(10);
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    uint8 i = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        // Only blink LED for 5 seconds, it is annoying as it keep on blinking if wifi in not on
        if (i <= 10)
        {
            digitalWrite(LED_BUILTIN, LOW);
            delay(250);
            digitalWrite(LED_BUILTIN, HIGH);
            delay(250);
            // Incrementing `i` here because it will not overflow
            i++;
        }
        else
            delay(500);
    }

    // Blink builtin LED fast to show wifi is connected
    for (int i = 0; i < 3; i++)
    {
        digitalWrite(LED_BUILTIN, LOW);
        delay(50);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(50);
    }

    randomSeed(micros());

    Serial.println();
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

void callback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");

    // Convert payload to string for easier comparison
    String message;
    for (unsigned int i = 0; i < length; i++)
    {
        message += (char)payload[i];
    }
    Serial.println(message);

    // Command Handling
    if (String(topic) == topic_command)
    {
        if (message == "PING")
        {
            client.publish(topic_status, "εつ💦(‿ˠ‿) What's good, fam?");
        }
        else if (message == "VERSION")
        {
            String version_info = "We rockin' v" + firmware_version + " right now.";
            client.publish(topic_status, version_info.c_str());
        }
        else if (message == "FORCE_POWER_OFF_WIN_SERVER")
        {
            win_server.trigger(
                5000,
                "(☞ ͡° ͜ʖ ͡°)☞ Aight, I'm finna shut down win-server for real, it's gotta go.",
                "ᕙ(•̀ᗜ•́)ᕗ Win-server is out. It's a wrap",
                "Slow down fam, another message is in flight.");
        }
        else if (message == "FORCE_POWER_OFF_NAS_SERVER")
        {
            nas_server.trigger(
                5000,
                "(☞ ͡° ͜ʖ ͡°)☞ Yo, just heads up, I'm force-killing the nas-server right now.",
                "ᕙ(•̀ᗜ•́)ᕗ Shut down nas-server for real, we good.",
                "One thing at a time, bruh. Wait.");
        }
        else if (message == "POWER_ON_WIN_SERVER")
        {
            win_server.trigger(
                500,
                "(☞ ͡° ͜ʖ ͡°)☞ Bout to fire up win-server... ▄︻デ۪۞━一💥",
                "ᕙ(•̀ᗜ•́)ᕗ Win-server's back in the building. We live!",
                "One thing at a time, bruh. Wait.");
        }
        else if (message == "POWER_ON_NAS_SERVER")
        {
            nas_server.trigger(
                500,
                "(☞ ͡° ͜ʖ ͡°)☞ Bout to get nas-server poppin... ▄︻デ۪۞━一💥",
                "ᕙ(•̀ᗜ•́)ᕗ NAS-server's back in the mix. We rollin'.",
                "Slow down fam, another message is in flight.");
        }
        else if (message == "MAGIC_WAKE_NAS")
        {
            const int status = send_magic_packet();
            if (status == 1)
            {
                client.publish(topic_status, "(-_•)▄︻テحكـ━一💥 Shot that magic packet right into the NAS, it's finna wake up.");
            }
            else
            {
                client.publish(topic_status, "(,,>﹏<,,)👉👈 Nah bruh, that magic packet didn't even go through! 😢");
            }
        }
        else if (message == "FUCK_YOU")
        {
            client.publish(topic_status, "Fuck You 𝓷𝓲𝓰𝓰𝓪𝓪𝓪𝓪...");
            delay(100);
            client.publish(topic_status, "⎛⎝(`ᢍ´)⎠⎞ᵐᵘʰᵃʰᵃ");
            delay(100);
            client.publish(topic_status, "(-_•)╦̵̵̿╤─");
        }
        else if (message == "MIDDLE_FINGER")
        {
            client.publish(topic_status, "╭∩╮(•̀_·́)╭∩╮");
        }
        else if (message == "DIDDY")
        {
            client.publish(topic_status, "(≖‿≖) Heehee");
            delay(100);
            client.publish(topic_status, "𝓓𝓲𝓭𝓭𝔂 𝓽𝓲𝓶𝓮👅🧴🧴");
        }
        else if (message == "BITCH")
        {
            client.publish(topic_status, "(＾◡＾)っ✂╰⋃╯");
        }
        else if (message == "UWU")
        {
            client.publish(topic_status, "U⩊U");
        }
        else if (message == "REBOOT")
        {
            client.publish(topic_status, "Bout to restart, hold tight...");
            delay(500);
            ESP.restart();
        }
        else if (message == "RESET")
        {
            client.publish(topic_status, "Starting fresh, hold your horses.");
            delay(500);
            ESP.reset();
        }
        else
        {
            client.publish(topic_status, "¯\\_(ツ)_/¯ Whatchu mean? I don't know that one.");
        }
    }
}

void reconnect()
{
    // Check if we're connected
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(client_id, mqtt_user, mqtt_password))
    {
        Serial.println("connected");
        // Once connected, publish an announcement...
        client.publish(topic_status, "(=^◡^=) Yo Nigga, I'm live! Let's get it!");
        String version_info = "System's at version v" + firmware_version + ", we stayin' current.";
        String local_ip = WiFi.localIP().toString();
        String update_url_info = "Update server's live! Head to http://" + mdns_hostname + ".local or http://" + local_ip + " and lock in with your info.";
        client.publish(topic_status, version_info.c_str());
        client.publish(topic_status, update_url_info.c_str());
        // ... and resubscribe
        client.subscribe(topic_command);
    }
    else
    {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.printf(" will try again in %0.2f seconds", static_cast<double>(mqtt_reconnect_interval / 1000));
    }
}

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
    win_server.init(POWER_PIN_WIN_SERVER, "win-server");
    nas_server.init(POWER_PIN_NAS_SERVER, "nas-server");

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

    setup_wifi();

    setup_webupdater();

    espClient.setInsecure();

    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
}

void loop()
{
    http_server.handleClient();
    MDNS.update();
    const unsigned long current_millis = millis();
    if (current_millis - mqtt_reconnect_current_millis > mqtt_reconnect_interval)
    {
        if (!client.connected())
            reconnect();
        mqtt_reconnect_current_millis = current_millis;
    }
    client.loop();
    win_server.update();
    nas_server.update();
}
