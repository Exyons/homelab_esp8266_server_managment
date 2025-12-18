#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Updater.h>

const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

const char *www_username = "admin";
const char *www_password = "admin";

ESP8266WebServer server(80);

const char *custom_html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Device Update</title>
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
        #progress-container { width: 100%; background-color: var(--border-color); border-radius: 5px; margin-top: 1rem; display: none; overflow: hidden; }
        #progress-bar { width: 0%; height: 20px; background-color: #28a745; text-align: center; line-height: 20px; color: white; transition: width 0.1s ease; }
        #status { margin-top: 1rem; color: var(--status-color); }
        .theme-toggle { position: absolute; top: 20px; right: 20px; cursor: pointer; font-size: 24px; user-select: none; }
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
                    // progressBar.innerText = percent + "%";
                    var msgs = [                                                                                                                                                                    │
                    "Gettin' that system refresh...",                                                                                                                                           │
                    "Bout to level up the firmware...",                                                                                                                                         │
                    "Uploading that good stuff...",                                                                                                                                             │
                    "Hold tight, we workin'...",                                                                                                                                                │
                    "Sending those bits, fam...",                                                                                                                                               │
                    "Almost there, stay chill...",                                                                                                                                              │
                    "Finna be a new machine...",                                                                                                                                                │
                    "Just a sec, G...",                                                                                                                                                         │
                    "Loading that heat...",                                                                                                                                                     │
                    "Trust the process..."                                                                                                                                                      │
                ];                                                                                                                                                                              │
                                                                                                                                                                                                │
                // Change text every 5% to avoid flickering                                                                                                                                     │
                if (percent % 5 === 0) {                                                                                                                                                        │
                    progressBar.innerText = msgs[Math.floor(Math.random() * msgs.length)];                                                                                                      │
                }                                                                                                                                                                               │
                if (progressBar.innerText === "0%" || progressBar.innerText === "") {                                                                                                           │
                    progressBar.innerText = msgs[Math.floor(Math.random() * msgs.length)];                                                                                                     │
                }                                                                                                                                                                               │
            }, false);

            // Completion handler
            xhr.onload = function() {
                var statusDiv = document.getElementById('status');
                if (xhr.status === 200) {
                    var countdown = 10;
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
              {
        if (!server.authenticate(www_username, www_password)) {
            return server.requestAuthentication();
        }
        server.send(200, "text/html", custom_html); });

    // Handle the upload
    server.on("/update", HTTP_POST, []()
              {
        if (!server.authenticate(www_username, www_password)) {
            return server.requestAuthentication();
        }
        server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        delay(1000);
        ESP.restart(); }, []()
              {
        if (!server.authenticate(www_username, www_password)) {
            return;
        }
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

    // Handle 404
    server.onNotFound([]()
                      {
        if (!server.authenticate(www_username, www_password)) {
            return server.requestAuthentication();
        }
        server.send(404, "text/html", not_found_html); });

    server.begin();
}

void loop()
{
    server.handleClient();
}
