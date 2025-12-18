#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Updater.h>

// Web updater setup
const char *mdns_hostname = MDNS_HOSTNAME;
const char *update_username = UPDATE_USERNAME;
const char *update_password = UPDATE_PASSWORD;
const char *custom_html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Device Update</title>
    <style>
        :root{--bg-color:#f4f4f9;--card-bg:#fff;--text-color:#333;--border-color:#ccc;--btn-bg:#007bff;--btn-hover:#0056b3;--status-color:#555;--hover-border:#007bff}[data-theme=dark]{--bg-color:#121212;--card-bg:#1e1e1e;--text-color:#e0e0e0;--border-color:#444;--btn-bg:#0069d9;--btn-hover:#0056b3;--status-color:#aaa;--hover-border:#0069d9}body{font-family:Segoe UI,Tahoma,Geneva,Verdana,sans-serif;background-color:var(--bg-color);color:var(--text-color);display:flex;justify-content:center;align-items:center;height:100vh;margin:0;transition:background-color .3s,color .3s}.container{background:var(--card-bg);padding:2rem;border-radius:10px;box-shadow:0 4px 6px rgba(0,0,0,.1);width:100%;max-width:400px;text-align:center;transition:background-color .3s}h2{margin-bottom:1.5rem;color:var(--text-color)}.file-upload{position:relative;display:inline-block;width:100%;margin-bottom:1rem}input[type=file]{display:none}.custom-file-upload{border:2px dashed var(--border-color);display:inline-block;padding:10px 12px;cursor:pointer;width:100%;box-sizing:border-box;border-radius:5px;color:var(--text-color);transition:all .3s}.custom-file-upload:hover{border-color:var(--hover-border);color:var(--hover-border)}.btn{background-color:var(--btn-bg);color:#fff;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;font-size:16px;width:100%;transition:background .3s}.btn:hover{background-color:var(--btn-hover)}.btn:disabled{background-color:#ccc;cursor:not-allowed}#progress-container{width:100%;background-color:var(--border-color);border-radius:5px;margin-top:1rem;display:none;overflow:hidden}#progress-bar{width:0;height:20px;background-color:#28a745;text-align:center;line-height:20px;color:#fff;transition:width .1s ease}#status{margin-top:1rem;color:var(--status-color)}.theme-toggle{position:absolute;top:20px;right:20px;cursor:pointer;font-size:24px;user-select:none}
    </style>
</head>
<body>
    <div class="theme-toggle" id="theme-toggle" onclick="toggleTheme()">🌙</div>
    <div class="container">
        <h2>Firmware Update</h2>
        <div class="file-upload">
            <label for="file-input" class="custom-file-upload" id="file-label">
                Select Firmware (.bin)
            </label>
            <input id="file-input" type="file" accept=".bin" onchange="updateFileName()">
        </div>
        <button id="upload-btn" class="btn" onclick="uploadFirmware()">Update Device</button>
        
        <div id="progress-container">
            <div id="progress-bar">0%</div>
        </div>
        <div id="status"></div>
    </div>

    <script>
        function toggleTheme(){const e=document.documentElement,t="dark"===e.getAttribute("data-theme")?"light":"dark";e.setAttribute("data-theme",t),localStorage.setItem("theme",t),document.getElementById("theme-toggle").innerText="dark"===t?"☀️":"🌙"}function updateFileName(){var e=document.getElementById("file-input"),t=document.getElementById("file-label");e.files&&e.files.length>0?t.textContent=e.files[0].name:t.textContent="Select Firmware (.bin)"}function uploadFirmware(){var e=document.getElementById("file-input");if(0!==e.files.length){var t=e.files[0],n=new FormData;n.append("update",t);var o=new XMLHttpRequest;document.getElementById("upload-btn").disabled=!0,document.getElementById("progress-container").style.display="block",document.getElementById("status").innerText="Uploading...",o.upload.addEventListener("progress",(function(e){if(e.lengthComputable){var t=Math.round(e.loaded/e.total*100),n=document.getElementById("progress-bar");n.style.width=t+"%",n.innerText=t+"%"}}),!1),o.onload=function(){var e=document.getElementById("status");if(200===o.status){var t=10;e.innerHTML="Update Success! Rebooting... <br> Page will reload in <span id='count'>"+t+"</span>s",document.getElementById("progress-bar").style.backgroundColor="#28a745";var n=setInterval((function(){t--,document.getElementById("count").innerText=t,t<=0&&(clearInterval(n),window.location.reload())}),1e3)}else e.innerText="Update Failed. Error: "+o.statusText,document.getElementById("progress-bar").style.backgroundColor="#dc3545",document.getElementById("upload-btn").disabled=!1},o.onerror=function(){document.getElementById("status").innerText="Network Error.",document.getElementById("upload-btn").disabled=!1},o.open("POST","/update"),o.send(n)}else alert("Please select a file first.")}!function(){const e=localStorage.getItem("theme")||"light";document.documentElement.setAttribute("data-theme",e),document.getElementById("theme-toggle").innerText="dark"===e?"☀️":"🌙"}();
    </script>
</body>
</html>
)rawliteral";

const char *not_found_html = R"rawliteral(
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

void blink_led_fast()
{
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
}

void blink_led_slow()
{
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
    digitalWrite(LED_BUILTIN, HIGH);
}

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
            Serial.println("Ping received. Sending Pong.");
            client.publish(topic_status, "PONG 🏓");
            blink_led_fast();
        }
        else if (message == "FORCE_POWER_OFF_WIN_SERVER")
        {
            client.publish(topic_status, "INFO: Powering off win-server forcefully... 😱");
            digitalWrite(POWER_PIN_WIN_SERVER, LOW);
            delay(5000);
            digitalWrite(POWER_PIN_WIN_SERVER, HIGH);
            client.publish(topic_status, "INFO: Done!! 😃");
        }
        else if (message == "FORCE_POWER_OFF_NAS_SERVER")
        {
            client.publish(topic_status, "INFO: Powering off nas-server forcefully... 😱");
            digitalWrite(POWER_PIN_NAS_SERVER, LOW);
            delay(5000);
            digitalWrite(POWER_PIN_NAS_SERVER, HIGH);
            client.publish(topic_status, "INFO: Done!! 😃");
        }
        else if (message == "POWER_ON_WIN_SERVER")
        {
            client.publish(topic_status, "INFO: Powering on win-server... ☺️");
            digitalWrite(POWER_PIN_WIN_SERVER, LOW);
            delay(500);
            digitalWrite(POWER_PIN_WIN_SERVER, HIGH);
            client.publish(topic_status, "INFO: Done!! 😃");
        }
        else if (message == "POWER_ON_NAS_SERVER")
        {
            client.publish(topic_status, "INFO: Powering on nas-server... ☺️");
            digitalWrite(POWER_PIN_NAS_SERVER, LOW);
            delay(500);
            digitalWrite(POWER_PIN_NAS_SERVER, HIGH);
            client.publish(topic_status, "INFO: Done!! 😃");
        }
        else if (message == "MAGIC_WAKE_NAS")
        {
            const int status = send_magic_packet();
            if (status == 1)
            {
                // Serial.println("Magic Packet Sent to NAS Server");
                client.publish(topic_status, "\\033[0;32mINFO: 🪄 📦 Sent to NAS Server!! ☺️");
            }
            else
            {
                client.publish(topic_status, "ERROR: 🪄 📦 not sent!!! 😢");
            }
            blink_led_fast();
        }
        else
        {
            // Serial.println("Unknown command");
            client.publish(topic_status, "Unknown Command");
        }
    }
}

void reconnect()
{
    // Loop until we're reconnected
    while (!client.connected())
    {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (client.connect(client_id, mqtt_user, mqtt_password))
        {
            Serial.println("connected");
            // Once connected, publish an announcement...
            client.publish(topic_status, "ESP8266 Is Online Bayba Wohoo!!! 😆");
            // ... and resubscribe
            client.subscribe(topic_command);
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
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
        http_server.send(200, "text/html", custom_html); });

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
    Serial.printf("HTTPUpdateServer ready! Open http://%s.local in your browser and login with username '%s' and password '%s'\n", mdns_hostname, update_username, update_password);
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
    if (!client.connected())
    {
        reconnect();
    }
    client.loop();
}
