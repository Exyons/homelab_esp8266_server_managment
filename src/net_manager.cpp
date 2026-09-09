#include "net_manager.h"
#include "config_store.h"
#include "log_store.h"
#include "web_server.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>

static const unsigned long STA_CONNECT_TIMEOUT_MS = 30000;
static const unsigned long AP_STA_RETRY_MS        = 300000;   // 5 minutes
static const unsigned long RESET_HOLD_MS          = 5000;
static const unsigned long RESET_ACQUIRE_WINDOW_MS = 5000;
static const uint8_t       FLASH_BUTTON_PIN       = 0;         // GPIO0
static const byte          DNS_PORT               = 53;

static NetState     g_state          = NET_AP;
static unsigned long g_state_entered = 0;
static unsigned long g_last_retry    = 0;
static char          g_ap_ssid[33]   = {0};
static DNSServer     g_dns;
static bool          g_dns_active    = false;
static IPAddress     g_bound_ip;      // address the listener/responder are bound to

// The FLASH button cannot be sampled during power-on: holding GPIO0 low as the
// device comes out of reset selects the ROM UART bootloader instead of running
// firmware. So the button is sampled only once firmware is up, and this call
// opens a real acquisition window: for RESET_ACQUIRE_WINDOW_MS it polls GPIO0
// waiting for a press, and once a press is seen the user must keep holding for
// a further RESET_HOLD_MS (LED blinking) to confirm. Releasing during that
// confirmation aborts the reset. The cost is a fixed ~5s of boot delay on every
// boot; that is the price of the promised press-after-power-on window, and it
// must also apply to STA boots, since the lockout this recovers from (valid
// config, unknown updater password, unreachable broker) is an STA lockout.
static bool flash_button_held_for_reset() {
    pinMode(FLASH_BUTTON_PIN, INPUT_PULLUP);
    Serial.println(F("Hold FLASH within 5s for factory reset..."));

    const unsigned long window_start = millis();
    while (millis() - window_start < RESET_ACQUIRE_WINDOW_MS) {
        if (digitalRead(FLASH_BUTTON_PIN) == LOW) {
            Serial.println(F("FLASH held; hold 5s for factory reset..."));
            const unsigned long start = millis();
            while (millis() - start < RESET_HOLD_MS) {
                if (digitalRead(FLASH_BUTTON_PIN) != LOW) return false;
                digitalWrite(LED_BUILTIN, LOW);
                delay(50);
                digitalWrite(LED_BUILTIN, HIGH);
                delay(50);
            }
            log_add("FLASH button held: erasing saved settings");
            return true;
        }
        delay(10);
    }
    return false;
}

static void build_ap_ssid() {
    snprintf(g_ap_ssid, sizeof(g_ap_ssid), "%s-%06X", AP_SSID_PREFIX, ESP.getChipId());
}

static void enter_ap() {
    build_ap_ssid();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(g_ap_ssid, AP_PASSWORD);
    const IPAddress ip = WiFi.softAPIP();

    g_dns.setErrorReplyCode(DNSReplyCode::NoError);
    g_dns_active = g_dns.start(DNS_PORT, "*", ip);

    g_state         = NET_AP;
    g_state_entered = millis();
    g_last_retry    = millis();

    log_add("Setup hotspot active: %s at http://%s", g_ap_ssid, ip.toString().c_str());
    web_on_network_up();          // softAP already holds 192.168.4.1
}

static void enter_sta() {
    if (g_dns_active) { g_dns.stop(); g_dns_active = false; }
    WiFi.softAPdisconnect(true);  // drop any softAP left from AP mode
    WiFi.mode(WIFI_STA);
    // Station hostname must be set before begin() or it is omitted from the
    // DHCP request (option 12), so the router lists the device as unnamed and
    // router-side name resolution never works.
    if (config().mdns_host[0] != '\0') {
        WiFi.hostname(config().mdns_host);
    }
    WiFi.begin(config().wifi_ssid, config().wifi_psk);
    g_bound_ip      = IPAddress(0, 0, 0, 0);
    g_state         = NET_STA_CONNECTING;
    g_state_entered = millis();
    log_add("Joining WiFi network \"%s\"", config().wifi_ssid);
}

void net_begin() {
    Serial.printf("Reset reason: %s\n", ESP.getResetReason().c_str());
    WiFi.persistent(false);       // don't rewrite WiFi config to flash each boot
    const bool reset_requested = flash_button_held_for_reset();
    if (reset_requested) {
        config_store_factory_reset();
    }
    const NetState want = net_initial_state(
        config_is_provisioned(config()), config().ap_forced, reset_requested);

    if (want == NET_AP) enter_ap();
    else                enter_sta();
}

void net_loop() {
    if (g_dns_active) g_dns.processNextRequest();

    switch (g_state) {
    case NET_STA_CONNECTING:
        // WL_CONNECTED means *associated*, which on the ESP8266 can be reported
        // before DHCP has handed over an address. Binding then gives the
        // listener and the mDNS responder 0.0.0.0 and neither answers, which is
        // why the UI was unreachable until a manual reset happened to win the
        // race. Wait for a real address before declaring the network up.
        if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
            g_state         = NET_STA_CONNECTED;
            g_state_entered = millis();
            g_bound_ip      = WiFi.localIP();
            log_add("WiFi connected as %s, signal %d dBm",
                    g_bound_ip.toString().c_str(), WiFi.RSSI());
            web_on_network_up();
            for (int i = 0; i < 3; i++) {          // connected blink
                digitalWrite(LED_BUILTIN, LOW);  delay(50);
                digitalWrite(LED_BUILTIN, HIGH); delay(50);
            }
        } else if (millis() - g_state_entered >= STA_CONNECT_TIMEOUT_MS) {
            log_add("WiFi did not connect within 30s, opening setup hotspot");
            enter_ap();
        }
        break;

    case NET_STA_CONNECTED:
        if (WiFi.status() != WL_CONNECTED) {
            log_add("WiFi connection lost, reconnecting");
            enter_sta();
        } else if (WiFi.localIP() != IPAddress(0, 0, 0, 0) &&
                   WiFi.localIP() != g_bound_ip) {
            // DHCP renewed onto a different address; re-bind or we keep
            // listening on one nobody is talking to.
            g_bound_ip = WiFi.localIP();
            log_add("Router assigned a new address: %s", g_bound_ip.toString().c_str());
            web_on_network_up();
        }
        break;

    case NET_AP:
        // Someone is associated with the setup AP, so they are probably mid
        // provisioning (or mid firmware upload). enter_sta() would drop the
        // softAP under them. Defer, and restart the retry clock so the full
        // interval elapses after the last client leaves.
        if (WiFi.softAPgetStationNum() > 0) {
            g_last_retry = millis();
            break;
        }
        // Self-heal: retry the station connection periodically unless the user
        // explicitly pinned AP mode.
        if (!config().ap_forced && config_is_provisioned(config()) &&
            millis() - g_last_retry >= AP_STA_RETRY_MS) {
            g_last_retry = millis();
            log_add("Retrying the saved WiFi network");
            enter_sta();
        }
        break;
    }
}

NetState    net_state()   { return g_state; }
bool        net_is_ap()   { return g_state == NET_AP; }
const char* net_ap_ssid() { return g_ap_ssid; }

void net_set_ap_forced(bool forced) {
    config().ap_forced = forced;
    // The HTTP 200 has already gone out by the time we get here, so a failed
    // write cannot be reported to the caller. Log it, so the serial console
    // disagrees with the UI rather than both silently claiming success.
    if (!config_store_save()) {
        log_add("WARNING: could not save hotspot setting to memory");
    }
    delay(200);
    ESP.restart();
}

void net_factory_reset_and_reboot() {
    config_store_factory_reset();
    delay(200);
    ESP.restart();
}
