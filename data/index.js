// Detect whether the Bootstrap CDN stylesheet actually loaded. While joined
// to the device's own setup AP there is no internet, so it never does — and
// without this the page renders unstyled with invisible icon-only buttons.
// styles.css ships on the device and carries a .nobs fallback for that case.
(function () {
  try {
    var probe = document.createElement("div");
    probe.className = "d-none";
    document.body.appendChild(probe);
    var loaded = window.getComputedStyle(probe).display === "none";
    document.body.removeChild(probe);
    if (!loaded) document.documentElement.classList.add("nobs");
  } catch (e) {
    document.documentElement.classList.add("nobs");
  }
})();

function toggleTheme() {
  const html = document.documentElement;
  const current = html.getAttribute("data-bs-theme");
  const next = current === "dark" ? "light" : "dark";
  html.setAttribute("data-bs-theme", next);
  localStorage.setItem("theme", next);
  document.getElementById("theme-toggle").innerText =
    next === "dark" ? "☀️" : "🌙";
}

// Initialize Theme
(function () {
  const saved = localStorage.getItem("theme") || "light";
  document.documentElement.setAttribute("data-bs-theme", saved);
  document.getElementById("theme-toggle").innerText =
    saved === "dark" ? "☀️" : "🌙";
})();

// Initialize Bootstrap Tooltips & Fetch Version
window.onload = function () {
  // bootstrap.bundle.min.js is a CDN request and is absent in AP mode.
  // Tooltips are decorative; skip them rather than throwing, which would
  // abort the rest of window.onload including fetchVersion().
  if (typeof bootstrap !== "undefined" && bootstrap.Tooltip) {
    const tooltipTriggerList = document.querySelectorAll('[data-bs-toggle="tooltip"]');
    [...tooltipTriggerList].forEach(tooltipTriggerEl => {
      const t = new bootstrap.Tooltip(tooltipTriggerEl);
      tooltipTriggerEl.addEventListener('click', () => t.hide());
    });
  }
  
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
            json.firmware_version + " is what this ESP8266 is rockin' right now.";
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
  // I have moved the text into a span with JS acceptable variabl name
  // Now no need to select it using document.getElementById
  // Its id is `upload_btn_text`
  if (input.files && input.files.length > 0) {
    const filename = input.files[0].name;
    label.textContent = filename;
    if (filename.includes("filesystem")) {
      upload_btn_text.textContent = "Update Homie's Filesystem";
    } else {
      upload_btn_text.textContent = "Update Homie's Firmware";
    }
  } else {
    label.textContent = "Grab that .bin real quick.";
    upload_btn_text.textContent = "Update Homie";
  }
}

/* ---- Panel visibility -------------------------------------------------
   The info table and the settings form are mutually exclusive: opening one
   closes the other, and clicking a panel's own icon again closes it. Both
   start hidden via an inline style, so "block" is the only shown state.
   ----------------------------------------------------------------------- */
function panelShown(id) {
  return document.getElementById(id).style.display === "block";
}

function showPanel(id) {
  document.getElementById(id).style.display = "block";
}

function hidePanel(id) {
  document.getElementById(id).style.display = "none";
}

/* Auto-refresh: poll /info once a second while the panel is visible, and stop
   the moment it is hidden. Guarded by inFlight so a slow response on a busy
   device cannot queue up overlapping requests. */
var infoTimer = null;
var infoInFlight = false;

var activeInfoTab = "info";

// Switch between the System Info table and the event log, and refresh the
// pane being shown straight away rather than waiting for the next tick.
function showInfoTab(tab) {
  activeInfoTab = tab;
  var isInfo = tab === "info";
  document.getElementById("info-pane-info").style.display = isInfo ? "block" : "none";
  document.getElementById("info-pane-logs").style.display = isInfo ? "none" : "block";
  document.getElementById("tab-info").classList.toggle("active", isInfo);
  document.getElementById("tab-logs").classList.toggle("active", !isInfo);
  refreshActiveInfoTab();
}

function refreshActiveInfoTab() {
  if (activeInfoTab === "logs") fetchLogs();
  else fetchESPInfo();
}

function fetchLogs() {
  var list = document.getElementById("log-list");
  var xhr = new XMLHttpRequest();
  infoInFlight = true;
  xhr.onloadend = function () { infoInFlight = false; };
  xhr.open("GET", "/logs");
  xhr.onload = function () {
    if (xhr.status !== 200) { list.innerHTML = "<em>Could not load logs.</em>"; return; }
    var entries;
    try { entries = JSON.parse(xhr.response); } catch (e) { return; }
    if (!entries.length) { list.innerHTML = "<em>Nothing logged yet.</em>"; return; }

    // Only redraw when something changed, so the pane does not flicker or
    // fight the user's scroll position once a second.
    var stamp = entries.length + "|" + entries[entries.length - 1].t +
                "|" + entries[entries.length - 1].m;
    if (list.dataset.stamp === stamp) return;
    list.dataset.stamp = stamp;

    var atBottom = list.scrollHeight - list.scrollTop - list.clientHeight < 24;
    list.innerHTML = "";
    entries.forEach(function (e) {
      var row  = document.createElement("div");
      row.className = "log-row";
      var time = document.createElement("span");
      time.className = "log-time";
      time.innerText = e.t;
      var msg  = document.createElement("span");
      msg.className = "log-msg";
      msg.innerText = e.m;
      if (/fail|warning|lost|unreachable|could not|erasing/i.test(e.m)) {
        row.classList.add("log-warn");
      }
      row.append(time, msg);
      list.append(row);
    });
    if (atBottom) list.scrollTop = list.scrollHeight;
  };
  xhr.send();
}

function startInfoAutoRefresh() {
  if (infoTimer !== null) return;
  infoTimer = setInterval(function () {
    if (!panelShown("info-container")) { stopInfoAutoRefresh(); return; }
    if (infoInFlight) return;
    refreshActiveInfoTab();
  }, 1000);
}

function stopInfoAutoRefresh() {
  if (infoTimer === null) return;
  clearInterval(infoTimer);
  infoTimer = null;
}

// Bound to the info icon. Closes settings if it was open.
function toggleESPInfo() {
  if (panelShown("info-container")) {
    hidePanel("info-container");
    stopInfoAutoRefresh();
    return;
  }
  hidePanel("settings-container");
  refreshActiveInfoTab();
  startInfoAutoRefresh();
}

// Bound to the gear icon. Closes the info table if it was open.
function toggleSettings() {
  if (panelShown("settings-container")) {
    hidePanel("settings-container");
    return;
  }
  hidePanel("info-container");
  stopInfoAutoRefresh();          // info is being closed; stop polling
  fetchConfig();
}

function fetchESPInfo() {
  const esp_info_table = document.getElementById("esp-info-table");
  const info_container = document.getElementById("info-container");
  
  let xhr = new XMLHttpRequest();
  infoInFlight = true;
  xhr.onloadend = function () { infoInFlight = false; };
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
      // Show container
      showPanel("info-container");
    } else {
      esp_info_table.innerHTML = "<h3>Error Fetching Info</h3>";
      showPanel("info-container");
    }
  };
  xhr.send();
}

function hideESPInfo() {
  hidePanel("info-container");
  stopInfoAutoRefresh();
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
  document.getElementById("btn-info").disabled = true;
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
      document.getElementById("btn-info").disabled = false;
    }
  };

  xhr.onerror = function () {
    document.getElementById("status").innerText =
      "Network's trippin'. Can't send it, fam.";
    document.getElementById("upload-btn").disabled = false;
    document.getElementById("reboot-btn").disabled = false;
    document.getElementById("file-input").disabled = false;
    document.getElementById("btn-info").disabled = false;
  };

  xhr.open("POST", "/update");
  xhr.send(formData);
}

function fetchConfig() {
  fetch("/config")
    .then(function (r) { return r.json(); })
    .then(function (c) {
      document.getElementById("cfg-wifi-ssid").value = c.wifi_ssid || "";
      document.getElementById("cfg-mqtt-host").value = c.mqtt_host || "";
      document.getElementById("cfg-mqtt-port").value = c.mqtt_port || 8883;
      document.getElementById("cfg-mqtt-user").value = c.mqtt_user || "";
      document.getElementById("cfg-device-id").value = c.device_id || "";
      document.getElementById("cfg-mdns-host").value = c.mdns_host || "";
      document.getElementById("cfg-upd-user").value  = c.upd_user  || "";
      document.getElementById("cfg-ap-forced").checked = !!c.ap_forced;

      // Secrets are never sent by the device. A stored value shows as a
      // placeholder so the field can be left blank to keep it.
      setSecretPlaceholder("cfg-wifi-psk",  c.has_wifi_psk);
      setSecretPlaceholder("cfg-mqtt-pass", c.has_mqtt_pass);
      setSecretPlaceholder("cfg-upd-pass",  c.has_upd_pass);

      document.getElementById("cred-warning").style.display =
        c.default_creds ? "block" : "none";
      showPanel("settings-container");
    })
    .catch(function () { alert("Could not load config."); });
}

function setSecretPlaceholder(id, isSet) {
  var el = document.getElementById(id);
  el.value = "";
  el.placeholder = isSet ? "•••••• (unchanged)" : "not set";
}

function hideSettings() {
  hidePanel("settings-container");
}

function saveConfig(event) {
  event.preventDefault();
  var body = {
    wifi_ssid: document.getElementById("cfg-wifi-ssid").value,
    wifi_psk:  document.getElementById("cfg-wifi-psk").value,
    mqtt_host: document.getElementById("cfg-mqtt-host").value,
    mqtt_port: parseInt(document.getElementById("cfg-mqtt-port").value || "8883", 10),
    mqtt_user: document.getElementById("cfg-mqtt-user").value,
    mqtt_pass: document.getElementById("cfg-mqtt-pass").value,
    device_id: document.getElementById("cfg-device-id").value,
    mdns_host: document.getElementById("cfg-mdns-host").value,
    upd_user:  document.getElementById("cfg-upd-user").value,
    upd_pass:  document.getElementById("cfg-upd-pass").value
  };
  postJSON("/config", body, "Settings saved. Rebooting...");
}

function toggleApMode(enabled) {
  if (!confirm(enabled
      ? "Force AP mode? The device will leave your network and reboot."
      : "Leave AP mode and reconnect to WiFi?")) {
    document.getElementById("cfg-ap-forced").checked = !enabled;
    return;
  }
  postJSON("/ap_mode", { enabled: enabled }, "Switching mode. Rebooting...");
}

function factoryReset() {
  if (!confirm("Erase all settings and return to setup mode? This cannot be undone."))
    return;
  postJSON("/factory_reset", {}, "Config erased. Rebooting into AP mode...");
}

function postJSON(url, body, successMsg) {
  fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body)
  })
    .then(function (r) { return r.json().then(function (j) { return { ok: r.ok, j: j }; }); })
    .then(function (res) {
      if (!res.ok) { alert("Error: " + (res.j.error || "unknown")); return; }
      document.getElementById("status").innerText = successMsg;
      setTimeout(function () { location.reload(); }, 15000);
    })
    .catch(function () { alert("Request failed."); });
}
