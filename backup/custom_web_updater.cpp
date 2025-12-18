#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// core update library
#include <Updater.h>

const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

ESP8266WebServer server(80);

// 1. Define your Custom HTML/CSS here
const char *custom_html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>My Custom Updater</title>
  <style>
    body { font-family: sans-serif; background: #2c3e50; color: #ecf0f1; text-align: center; padding-top: 50px; }
    .card { background: #34495e; max-width: 400px; margin: 0 auto; padding: 20px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
    h1 { color: #e74c3c; margin-bottom: 20px; }
    input[type="file"] { margin-bottom: 20px; }
    button { background: #e67e22; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; font-size: 16px; }
    button:hover { background: #d35400; }
  </style>
</head>
<body>
  <div class="card">
    <h1>System Update</h1>
    <form method='POST' action='/update' enctype='multipart/form-data'>
      <input type='file' name='update'>
      <br>
      <button type='submit'>Update Firmware</button>
    </form>
    <p>Please do not turn off power while updating.</p>
  </div>
</body>
</html>
)rawliteral";

void setup()
{
    Serial.begin(115200);
    WiFi.begin(ssid, password);

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // 2. Serve your custom HTML page
    server.on("/", HTTP_GET, []()
              { server.send(200, "text/html", custom_html); });

    // 3. Handle the firmware upload
    server.on("/update", HTTP_POST, []()
              {
    // This callback runs when the upload is finished
    server.send(200, "text/plain", (Update.hasError()) ? "Update Failed" : "Update Success! Rebooting...");
    delay(1000);
    ESP.restart(); }, []()
              {
    // This callback runs *during* the upload
    HTTPUpload& upload = server.upload();
    
    if (upload.status == UPLOAD_FILE_START) {
      Serial.setDebugOutput(true);
      Serial.printf("Update: %s\n", upload.filename.c_str());
      
      // Determine if we are updating Sketch or Filesystem
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      
      if (!Update.begin(maxSketchSpace)) { 
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      // Write the received chunk to flash
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      // Finalize the update
      if (Update.end(true)) { 
        Serial.printf("Update Success: %uB\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
      Serial.setDebugOutput(false);
    } });

    server.begin();
}

void loop()
{
    server.handleClient();
}