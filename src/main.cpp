#include <Arduino.h>
#include <LittleFS.h>
#include "config_store.h"
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

    config_store_begin();   // must precede net_begin(); it reads the config
    net_begin();
    web_begin();
    mqtt_begin(&win_server, &nas_server);
}

void loop() {
    net_loop();
    web_loop();
    mqtt_loop();
    win_server.update();
    nas_server.update();
}
