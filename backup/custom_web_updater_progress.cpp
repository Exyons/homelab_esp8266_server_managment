#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Updater.h>

const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

ESP8266WebServer server(80);

const char *custom_html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Device Update</title>
    <style>
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #f4f4f9; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
        .container { background: white; padding: 2rem; border-radius: 10px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); width: 100%; max-width: 400px; text-align: center; }
        h2 { color: #333; margin-bottom: 1.5rem; }
        .file-upload { position: relative; display: inline-block; width: 100%; margin-bottom: 1rem; }
        input[type="file"] { display: none; }
        .custom-file-upload { border: 2px dashed #ccc; display: inline-block; padding: 10px 12px; cursor: pointer; width: 100%; box-sizing: border-box; border-radius: 5px; color: #666; transition: all 0.3s; }
        .custom-file-upload:hover { border-color: #007bff; color: #007bff; }
        .btn { background-color: #007bff; color: white; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; width: 100%; transition: background 0.3s; }
        .btn:hover { background-color: #0056b3; }
        .btn:disabled { background-color: #ccc; cursor: not-allowed; }
        #progress-container { width: 100%; background-color: #e0e0e0; border-radius: 5px; margin-top: 1rem; display: none; overflow: hidden; }
        #progress-bar { width: 0%; height: 20px; background-color: #28a745; text-align: center; line-height: 20px; color: white; transition: width 0.1s ease; }
        #status { margin-top: 1rem; color: #555; }
    </style>
</head>
<body>
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
        function updateFileName() {
            var input = document.getElementById('file-input');
            var label = document.getElementById('file-label');
            if(input.files && input.files.length > 0) {
                label.innerText = input.files[0].name;
            } else {
                label.innerText = "Select Firmware (.bin)";
            }
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
            document.getElementById('progress-container').style.display = 'block';
            document.getElementById('status').innerText = "Uploading...";

            // Progress event
            xhr.upload.addEventListener("progress", function(e) {
                if (e.lengthComputable) {
                    var percent = Math.round((e.loaded / e.total) * 100);
                    var progressBar = document.getElementById('progress-bar');
                    progressBar.style.width = percent + "%";
                    progressBar.innerText = percent + "%";
                }
            }, false);

            // Completion handler
            xhr.onload = function() {
                var statusDiv = document.getElementById('status');
                if (xhr.status === 200) {
                    statusDiv.innerText = "Update Success! Rebooting...";
                    document.getElementById('progress-bar').style.backgroundColor = "#28a745";
                } else {
                    statusDiv.innerText = "Update Failed. Error: " + xhr.statusText;
                    document.getElementById('progress-bar').style.backgroundColor = "#dc3545";
                    document.getElementById('upload-btn').disabled = false;
                }
            };

            xhr.onerror = function() {
                document.getElementById('status').innerText = "Network Error.";
                document.getElementById('upload-btn').disabled = false;
            };

            xhr.open("POST", "/update");
            xhr.send(formData);
        }
    </script>
</body>
</html>
)rawliteral";

void setup()
{
    Serial.begin(115200);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nReady! IP: " + WiFi.localIP().toString());

    // Serve the HTML page
    server.on("/", HTTP_GET, []()
              { server.send(200, "text/html", custom_html); });

    // Handle the upload
    server.on("/update", HTTP_POST, []()
              {
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    delay(1000);
    ESP.restart(); }, []()
              {
    HTTPUpload& upload = server.upload();
    
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

    server.begin();
}

void loop()
{
    server.handleClient();
}
