#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Updater.h>

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
const char *firmware_version = TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_MINOR) "." TOSTRING(VERSION_PATCH);

// Addding PROGMEM moves the contents to flash memory, this frees up RAM
const char *index_html_top PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Nigga Firmware Updater</title>
    <style>
        :root {
            --bg-color: #f4f4f9;
            --card-bg: #ffffff;
            --text-color: #333333;
            --border-color: #cccccc;
            --btn-bg: #007bff;
            --btn-hover: #0056b3;
            --status-color: #555555;
            --hover-border: #007bff;
        }
        [data-theme="dark"] {
            --bg-color: #121212;
            --card-bg: #1e1e1e;
            --text-color: #e0e0e0;
            --border-color: #444444;
            --btn-bg: #0069d9;
            --btn-hover: #0056b3;
            --status-color: #aaaaaa;
            --hover-border: #0069d9;
        }
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: var(--bg-color); color: var(--text-color); display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; transition: background-color 0.3s, color 0.3s; }
        .container { background: var(--card-bg); padding: 2rem; border-radius: 10px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); width: 100%; max-width: 400px; text-align: center; transition: background-color 0.3s; }
        h2 { margin-bottom: 1.5rem; color: var(--text-color); }
        .file-upload { position: relative; display: inline-block; width: 100%; margin-bottom: 1rem; }
        input[type="file"] { display: none; }
        .custom-file-upload { border: 2px dashed var(--border-color); display: inline-block; padding: 10px 12px; cursor: pointer; width: 100%; box-sizing: border-box; border-radius: 5px; color: var(--text-color); transition: all 0.3s; }
        .custom-file-upload:hover { border-color: var(--hover-border); color: var(--hover-border); }
        .btn { background-color: var(--btn-bg); color: white; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; width: 100%; transition: background 0.3s; }
        .btn:hover { background-color: var(--btn-hover); }
        .btn:disabled { background-color: #ccc; cursor: not-allowed; }
        .btn-danger { background-color: #dc3545; margin-top: 1rem; }
        .btn-danger:hover { background-color: #c82333; }
        #progress-container { width: 100%; background-color: var(--border-color); border-radius: 5px; margin-top: 1rem; display: none; overflow: hidden; }
        #progress-bar { width: 0%; height: 20px; background-color: #28a745; text-align: center; line-height: 20px; color: white; transition: width 0.1s ease; }
        #status { margin-top: 1rem; color: var(--status-color); }
        .theme-toggle { position: absolute; top: 20px; right: 20px; cursor: pointer; font-size: 24px; user-select: none; }
    </style>
</head>
<body>
    <div class="theme-toggle" id="theme-toggle" onclick="toggleTheme()">🌙</div>
    <div class="container">
        <h2>Go 'head and get that system right</h2>
)rawliteral";

const char *index_html_bottom = R"rawliteral(
        <div class="file-upload">
            <label for="file-input" class="custom-file-upload" id="file-label">
                Grab that firmware.bin real quick.
            </label>
            <input id="file-input" type="file" accept=".bin" onchange="updateFileName()">
        </div>
        <button id="upload-btn" class="btn" onclick="uploadFirmware()">Update Nigga</button>
        <button id="reboot-btn" class="btn btn-danger" onclick="rebootDevice()">Reboot Nigga</button>
        
        <div id="progress-container">
            <div id="progress-bar">0%</div>
        </div>
        <div id="status"></div>
    </div>

    <script>
        function toggleTheme() {
            const html = document.documentElement;
            const current = html.getAttribute('data-theme');
            const next = current === 'dark' ? 'light' : 'dark';
            html.setAttribute('data-theme', next);
            localStorage.setItem('theme', next);
            document.getElementById('theme-toggle').innerText = next === 'dark' ? '☀️' : '🌙';
        }

        // Initialize Theme
        (function() {
            const saved = localStorage.getItem('theme') || 'light';
            document.documentElement.setAttribute('data-theme', saved);
            document.getElementById('theme-toggle').innerText = saved === 'dark' ? '☀️' : '🌙';
        })();

        function updateFileName() {
            var input = document.getElementById('file-input');
            var label = document.getElementById('file-label');
            if (input.files && input.files.length > 0) {
                label.textContent = input.files[0].name;
            } else {
                label.textContent = "Select Firmware (.bin)";
            }
        }

        function rebootDevice() {
            if (!confirm("Are you sure you want to reboot the device?")) return;
            
            var xhr = new XMLHttpRequest();
            xhr.open("POST", "/reboot");
            xhr.onload = function() {
                if (xhr.status === 200) {
                    alert("Device is rebooting. Page will reload in 5 seconds.");
                    setTimeout(function() { window.location.reload(); }, 5000);
                } else {
                    alert("Reboot failed.");
                }
            };
            xhr.send();
        }

        function uploadFirmware() {
            var input = document.getElementById('file-input');
            if(input.files.length === 0){
                alert("Please select a file first.");
                return;
            }
            
            var file = input.files[0];
            var formData = new FormData();
            formData.append("update", file);
            
            var xhr = new XMLHttpRequest();
            
            // UI updates
            document.getElementById('upload-btn').disabled = true;
            document.getElementById('reboot-btn').disabled = true;
            document.getElementById('file-input').disabled = true;
            document.getElementById('progress-container').style.display = 'block';
            document.getElementById('status').innerText = "Uploading...";

            // Progress event
            xhr.upload.addEventListener("progress", function(e) {
                if (e.lengthComputable) {
                    var percent = Math.round((e.loaded / e.total) * 100);
                    var progressBar = document.getElementById('progress-bar');
                    progressBar.style.width = percent + "%";
                    progressBar.innerText = percent + "%";
                    
                    var msgs = [
                        "Gettin' that system refresh...",
                        "Bout to level up the firmware...",
                        "Uploading that good stuff...",
                        "Hold tight, we workin'...",
                        "Sending those bits, fam...",
                        "Almost there, stay chill...",
                        "Finna be a new machine...",
                        "Just a sec, G...",
                        "Loading that heat...",
                        "Trust the process..."
                    ];

                    // Change text every 5% to avoid flickering
                    if (percent % 5 === 0) {
                        document.getElementById('status').innerText = msgs[Math.floor(Math.random() * msgs.length)];
                    }
                }
            }, false);

            // Completion handler
            xhr.onload = function() {
                var statusDiv = document.getElementById('status');
                if (xhr.status === 200) {
                    var countdown = 15;
                    statusDiv.innerHTML = "Update Success! Rebooting... <br> Page will reload in <span id='count'>" + countdown + "</span>s";
                    document.getElementById('progress-bar').style.backgroundColor = "#28a745";
                    
                    var timer = setInterval(function() {
                        countdown--;
                        document.getElementById('count').innerText = countdown;
                        if (countdown <= 0) {
                            clearInterval(timer);
                            window.location.reload();
                        }
                    }, 1000);
                } else {
                    statusDiv.innerText = "Nah bruh, update bricked. Error: " + xhr.statusText;
                    document.getElementById('progress-bar').style.backgroundColor = "#dc3545";
                    document.getElementById('upload-btn').disabled = false;
                    document.getElementById('reboot-btn').disabled = false;
                    document.getElementById('file-input').disabled = false;
                }
            };

            xhr.onerror = function() {
                document.getElementById('status').innerText = "Network's trippin'. Can't send it, fam.";
                document.getElementById('upload-btn').disabled = false;
                document.getElementById('reboot-btn').disabled = false;
                document.getElementById('file-input').disabled = false;
            };

            xhr.open("POST", "/update");
            xhr.send(formData);
        }
    </script>
</body>
</html>
)rawliteral";

const char *not_found_html PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>404 Not Found</title>
    <style>
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #f4f4f9; color: #333; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
        .container { text-align: center; background: white; padding: 3rem; border-radius: 10px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
        h1 { font-size: 4rem; margin: 0; color: #dc3545; }
        p { font-size: 1.5rem; margin: 1rem 0; }
        a { color: #007bff; text-decoration: none; font-weight: bold; }
        a:hover { text-decoration: underline; }
    </style>
</head>
<body>
    <div class="container">
        <h1>404</h1>
        <p>Oops! Page not found.</p>
        <a href="/">Go back to Updater</a>
    </div>
</body>
</html>
)rawliteral";

const unsigned long mqtt_reconnect_interval = 5000;
unsigned long mqtt_reconnect_current_millis = 0;

// Web updater setup
const char *mdns_hostname = MDNS_HOSTNAME;
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

unsigned long lastMsg = 0;

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

    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        digitalWrite(LED_BUILTIN, LOW);
        delay(250);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(250);
    }

    // Blink builyin LED fast to show wifi is connected
    for (int i = 0; i < 2; i++)
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
            digitalWrite(LED_BUILTIN, LOW);
            delay(100);
            digitalWrite(LED_BUILTIN, HIGH);
        }
        else if (message == "VERSION")
        {
            const char *version_string = "We rockin' v" TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_MINOR) "." TOSTRING(VERSION_PATCH) " right now.";
            client.publish(topic_status, version_string);
        }
        else if (message == "FORCE_POWER_OFF_WIN_SERVER")
        {
            client.publish(topic_status, "(☞ ͡° ͜ʖ ͡°)☞ Aight, I'm finna shut down win-server for real, it's gotta go.");
            digitalWrite(POWER_PIN_WIN_SERVER, LOW);
            delay(5000);
            digitalWrite(POWER_PIN_WIN_SERVER, HIGH);
            client.publish(topic_status, "ᕙ(•̀ ᗜ •́)ᕗ We good.");
        }
        else if (message == "FORCE_POWER_OFF_NAS_SERVER")
        {
            client.publish(topic_status, "(☞ ͡° ͜ʖ ͡°)☞ Yo, just heads up, I'm force-killing the nas-server right now.");
            digitalWrite(POWER_PIN_NAS_SERVER, LOW);
            delay(5000);
            digitalWrite(POWER_PIN_NAS_SERVER, HIGH);
            client.publish(topic_status, "ᕙ(•̀ ᗜ •́)ᕗ Bet.");
        }
        else if (message == "POWER_ON_WIN_SERVER")
        {
            client.publish(topic_status, "(☞ ͡° ͜ʖ ͡°)☞ Bout to fire up win-server... ▄︻デ۪۞━一💥");
            digitalWrite(POWER_PIN_WIN_SERVER, LOW);
            delay(500);
            digitalWrite(POWER_PIN_WIN_SERVER, HIGH);
            client.publish(topic_status, "ᕙ(•̀ ᗜ •́)ᕗ Say less.");
        }
        else if (message == "POWER_ON_NAS_SERVER")
        {
            client.publish(topic_status, "(☞ ͡° ͜ʖ ͡°)☞ Bout to get nas-server poppin... ▄︻デ۪۞━一💥");
            digitalWrite(POWER_PIN_NAS_SERVER, LOW);
            delay(500);
            digitalWrite(POWER_PIN_NAS_SERVER, HIGH);
            client.publish(topic_status, "ᕙ(•̀ ᗜ •́)ᕗ It's a wrap.");
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
            client.publish(topic_status, "ᶠᶸᶜᵏᵧₒᵤ!𝓷𝓲𝓰𝓰𝓪");
            delay(20);
            client.publish(topic_status, "⎛⎝(`ᢍ´)⎠⎞ᵐᵘʰᵃʰᵃ");
            delay(20);
            client.publish(topic_status, "(-_•)╦̵̵̿╤─");
        }
        else if (message == "MIDDLE_FINGER")
        {
            client.publish(topic_status, "╭∩╮(•̀_·́)╭∩╮");
        }
        else if (message == "DIDDY")
        {
            client.publish(topic_status, "(≖‿≖) Heehee");
            delay(25);
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
        const char *version_string = "System's at version v" TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_MINOR) "." TOSTRING(VERSION_PATCH) ", we stayin' current.";
        const char *update_info = "Update server's live! Head to http://" MDNS_HOSTNAME ".local and lock in with your info.";
        client.publish(topic_status, version_string);
        client.publish(topic_status, update_info);
        // ... and resubscribe
        // client.subscribe(topic_command);
    }
    else
    {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" will try again in 5 seconds");
    }
}

void setup_webupdater()
{
    MDNS.begin(mdns_hostname);

    // Serve the HTML page
    http_server.on("/", HTTP_GET, []()
                   {
        if (!http_server.authenticate(update_username, update_password)) {
            return http_server.requestAuthentication();
        }
        http_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        http_server.send(200, "text/html", ""); // Send headers first
        http_server.sendContent_P(index_html_top);

        String version_msg = "v" + String(firmware_version) + " is what this ESP8266 is rockin' right now.";
        String version_div = "<div id=\"version-status\" style=\"margin-bottom: 1rem; font-style: italic; color: var(--status-color);\">" + version_msg + "</div>";
        http_server.sendContent(version_div);
        http_server.sendContent_P(index_html_bottom); });

    // Handle reboot
    http_server.on("/reboot", HTTP_POST, []()
                   {
        if(!http_server.authenticate(update_username, update_password)){
            return http_server.requestAuthentication();
        } 
        http_server.send(200, "text/plain", "Bout to restart, hold tight...");
        delay(1000);
        ESP.restart(); });

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
      // Check if it's a FS or Sketch update based on space
      // Note: This simple logic assumes Sketch update. 
      // For safer FS/Sketch detection you might need additional logic or separate endpoints.
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(maxSketchSpace)) { 
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

    // Handle 404
    http_server.onNotFound([]()
                           {
        if (!http_server.authenticate(update_username, update_password)) {
            return http_server.requestAuthentication();
        }
        http_server.send(404, "text/html", not_found_html); });

    http_server.begin();

    MDNS.addService("http", "tcp", 80);
}

void setup()
{
    pinMode(POWER_PIN_NAS_SERVER, OUTPUT);
    digitalWrite(POWER_PIN_NAS_SERVER, HIGH); // Start with HIGH because for this board HIGH means LOW and vice-versa

    pinMode(POWER_PIN_WIN_SERVER, OUTPUT);
    digitalWrite(POWER_PIN_WIN_SERVER, HIGH);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    Serial.begin(115200);

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
}
