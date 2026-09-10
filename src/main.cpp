#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include "config_store.h"
#include "log_store.h"
#include "version.h"
#include "net_manager.h"
#include "web_server.h"
#include "mqtt_client.h"
#include "pulse_action.h"

// Required for ESP.getVcc() to return a real reading rather than a floating
// ADC. Makes A0 unavailable for external analog input, which this board does
// not use.
ADC_MODE(ADC_VCC);

static const int POWER_PIN_WIN_SERVER = 14;  // GPIO14 (D5)
static const int POWER_PIN_NAS_SERVER = 5;   // GPIO5  (D1)

PulseAction win_server;
PulseAction nas_server;

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    win_server.init(POWER_PIN_WIN_SERVER, "win-server", mqtt_publish_status);
    nas_server.init(POWER_PIN_NAS_SERVER, "nas-server", mqtt_publish_status);

    if (!LittleFS.begin()) {
        Serial.println(F("LittleFS mount failed; failsafe UI will be served."));
    }

    log_begin();
    log_info("Booted after %s, firmware v%s",
            ESP.getResetReason().c_str(), firmware_version.c_str());

    config_store_begin();   // must precede net_begin(); it reads the config
    web_begin();            // registers routes only — must precede net_begin(),
                            // which binds the listener via web_on_network_up()
    net_begin();
    mqtt_begin(&win_server, &nas_server);
}

// Periodic serial heartbeat, retired. It printed the same numbers every ten
// seconds whether or not anything happened, which buried the lines that
// mattered. Health is now event-driven and readable in the web UI at
// GET /logs; see log_store.h.
//
// static void heap_heartbeat() {
//     static unsigned long last = 0;
//     if (millis() - last < 10000) return;
//     last = millis();
//     Serial.printf("[health] heap=%u largest=%u frag=%u%% wifi=%d mqtt=%d\n",
//                   ESP.getFreeHeap(), ESP.getMaxFreeBlockSize(),
//                   ESP.getHeapFragmentation(), WiFi.status(), mqtt_connected());
// }

void loop() {
    log_check_heap();      // silent unless memory actually drops
    net_loop();
    web_loop();
    mqtt_loop();
    win_server.update();
    nas_server.update();
}
