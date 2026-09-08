#include "net_manager.h"
#include "config_store.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>

static const unsigned long STA_CONNECT_TIMEOUT_MS = 30000;
static const unsigned long AP_STA_RETRY_MS        = 300000;   // 5 minutes
static const unsigned long RESET_HOLD_MS          = 5000;
static const uint8_t       FLASH_BUTTON_PIN       = 0;         // GPIO0
static const byte          DNS_PORT               = 53;

static NetState     g_state          = NET_AP;
static unsigned long g_state_entered = 0;
static unsigned long g_last_retry    = 0;
static char          g_ap_ssid[33]   = {0};
static DNSServer     g_dns;
static bool          g_dns_active    = false;

// The FLASH button cannot be sampled during power-on: holding GPIO0 low at
// reset enters the ROM bootloader. Sample after boot instead, so the user
// presses and holds within the first five seconds of running firmware.
static bool flash_button_held_for_reset() {
    pinMode(FLASH_BUTTON_PIN, INPUT_PULLUP);
    if (digitalRead(FLASH_BUTTON_PIN) != LOW) return false;

    Serial.println(F("FLASH held; hold 5s for factory reset..."));
    const unsigned long start = millis();
    while (millis() - start < RESET_HOLD_MS) {
        if (digitalRead(FLASH_BUTTON_PIN) != LOW) return false;
        digitalWrite(LED_BUILTIN, LOW);
        delay(50);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(50);
    }
    Serial.println(F("Factory reset triggered."));
    return true;
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

    Serial.printf("AP up: %s  http://%s\n", g_ap_ssid, ip.toString().c_str());
}

static void enter_sta() {
    if (g_dns_active) { g_dns.stop(); g_dns_active = false; }
    WiFi.mode(WIFI_STA);
    WiFi.begin(config().wifi_ssid, config().wifi_psk);
    g_state         = NET_STA_CONNECTING;
    g_state_entered = millis();
    Serial.printf("Connecting to %s\n", config().wifi_ssid);
}

void net_begin() {
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
        if (WiFi.status() == WL_CONNECTED) {
            g_state         = NET_STA_CONNECTED;
            g_state_entered = millis();
            Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
            for (int i = 0; i < 3; i++) {          // connected blink
                digitalWrite(LED_BUILTIN, LOW);  delay(50);
                digitalWrite(LED_BUILTIN, HIGH); delay(50);
            }
        } else if (millis() - g_state_entered >= STA_CONNECT_TIMEOUT_MS) {
            Serial.println(F("STA timeout; falling back to AP."));
            enter_ap();
        }
        break;

    case NET_STA_CONNECTED:
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("WiFi lost; reconnecting."));
            enter_sta();
        }
        break;

    case NET_AP:
        // Self-heal: retry the station connection periodically unless the user
        // explicitly pinned AP mode.
        if (!config().ap_forced && config_is_provisioned(config()) &&
            millis() - g_last_retry >= AP_STA_RETRY_MS) {
            g_last_retry = millis();
            Serial.println(F("Retrying STA from AP mode."));
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
    config_store_save();
    delay(200);
    ESP.restart();
}

void net_factory_reset_and_reboot() {
    config_store_factory_reset();
    delay(200);
    ESP.restart();
}
