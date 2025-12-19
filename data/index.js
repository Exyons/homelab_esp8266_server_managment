function toggleTheme() {
  const html = document.documentElement;
  const current = html.getAttribute("data-theme");
  const next = current === "dark" ? "light" : "dark";
  html.setAttribute("data-theme", next);
  localStorage.setItem("theme", next);
  document.getElementById("theme-toggle").innerText =
    next === "dark" ? "☀️" : "🌙";
}

// Initialize Theme
(function () {
  const saved = localStorage.getItem("theme") || "light";
  document.documentElement.setAttribute("data-theme", saved);
  document.getElementById("theme-toggle").innerText =
    saved === "dark" ? "☀️" : "🌙";
})();

// Fetch Version on Load
window.onload = function () {
  fetchVersion();
};

function fetchVersion() {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/info");
  xhr.onload = function () {
    if (xhr.status == 200) {
      try {
        var json = JSON.parse(xhr.response);
        if (json.firmware_version) {
          document.getElementById("version-status").innerText =
            "v" + json.firmware_version + " is what this ESP8266 is rockin' right now.";
        }
      } catch (e) {
        console.error("JSON Error");
      }
    }
  };
  xhr.send();
}

function updateFileName() {
  const input = document.getElementById("file-input");
  const label = document.getElementById("file-label");
  const upload_btn = document.getElementById("upload-btn");
  if (input.files && input.files.length > 0) {
    const filename = input.files[0].name;
    label.textContent = filename;
    if (filename.includes("filesystem")) {
      upload_btn.textContent = "Update Nigga's Filesystem";
    } else {
      upload_btn.textContent = "Update Nigga's Firmware";
    }
  } else {
    label.textContent = "Grab that .bin real quick.";
  }
}

function fetchESPInfo() {
  const esp_info_table = document.getElementById("esp-info-table");
  let xhr = new XMLHttpRequest();
  xhr.open("GET", "/info");
  xhr.onload = function () {
    if (xhr.status == 200) {
      // Clean table
      esp_info_table.innerHTML = "";
      const json_string = xhr.response;
      const json_object = JSON.parse(json_string);
      for (const key in json_object) {
        const table_row = document.createElement("tr");
        const table_cell_key = document.createElement("td");
        const table_cell_value = document.createElement("td");
        table_cell_key.innerText = key;
        table_cell_value.innerText = json_object[key];
        table_row.append(table_cell_key);
        table_row.append(table_cell_value);
        esp_info_table.append(table_row);
      }
    } else {
      esp_info_table.innerHTML = "<h3>Error Fetching Info</h3>";
    }
  };
  xhr.send();
}

function rebootDevice() {
  if (!confirm("Are you sure you want to reboot the device?")) return;

  let xhr = new XMLHttpRequest();
  xhr.open("POST", "/reboot");
  xhr.onload = function () {
    if (xhr.status === 200) {
      alert("Device is rebooting. Page will reload in 5 seconds.");
      setTimeout(function () {
        location.reload();
      }, 5000);
    } else {
      alert("Reboot failed.");
    }
  };
  xhr.send();
}

function uploadFirmware() {
  const input = document.getElementById("file-input");
  if (input.files.length === 0) {
    alert("Please select a file first.");
    return;
  }

  const file = input.files[0];
  let formData = new FormData();
  formData.append("update", file);

  let xhr = new XMLHttpRequest();

  // UI updates
  document.getElementById("upload-btn").disabled = true;
  document.getElementById("reboot-btn").disabled = true;
  document.getElementById("file-input").disabled = true;
  document.getElementById("info-btn").disabled = true;
  document.getElementById("progress-container").style.display = "block";
  document.getElementById("status").innerText = "Uploading...";

  // Progress event
  xhr.upload.addEventListener(
    "progress",
    function (e) {
      if (e.lengthComputable) {
        let percent = Math.round((e.loaded / e.total) * 100);
        const progressBar = document.getElementById("progress-bar");
        progressBar.style.width = percent + "%";
        progressBar.innerText = percent + "%";

        const msgs = [
          "Gettin' that system refresh...",
          "Bout to level up the firmware...",
          "Uploading that good stuff...",
          "Hold tight, we workin'...",
          "Sending those bits, fam...",
          "Almost there, stay chill...",
          "Finna be a new machine...",
          "Just a sec, G...",
          "Loading that heat...",
          "Trust the process...",
        ];

        // Change text every 5% to avoid flickering
        if (percent % 5 === 0) {
          document.getElementById("status").innerText =
            msgs[Math.floor(Math.random() * msgs.length)];
        }
      }
    },
    false
  );

  // Completion handler
  xhr.onload = function () {
    const statusDiv = document.getElementById("status");
    if (xhr.status === 200) {
      let countdown = 15;
      statusDiv.innerHTML =
        "Update Success! Rebooting... <br> Page will reload in <span id='count'>" +
        countdown +
        "</span>s";
      document.getElementById("progress-bar").style.backgroundColor = "#28a745";

      let timer = setInterval(function () {
        countdown--;
        document.getElementById("count").innerText = countdown;
        if (countdown <= 0) {
          clearInterval(timer);
          location.reload();
        }
      }, 1000);
    } else {
      statusDiv.innerText =
        "Nah bruh, update bricked. Error: " + xhr.statusText;
      document.getElementById("progress-bar").style.backgroundColor = "#dc3545";
      document.getElementById("upload-btn").disabled = false;
      document.getElementById("reboot-btn").disabled = false;
      document.getElementById("file-input").disabled = false;
      document.getElementById("info-btn").disabled = false;
    }
  };

  xhr.onerror = function () {
    document.getElementById("status").innerText =
      "Network's trippin'. Can't send it, fam.";
    document.getElementById("upload-btn").disabled = false;
    document.getElementById("reboot-btn").disabled = false;
    document.getElementById("file-input").disabled = false;
    document.getElementById("info-btn").disabled = false;
  };

  xhr.open("POST", "/update");
  xhr.send(formData);
}
