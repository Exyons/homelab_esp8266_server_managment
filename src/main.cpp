#include <LittleFS.h>
#include "pulse_action.h"
#include "config_store.h"
#include "net_manager.h"
#include "mqtt_client.h"
#include "web_server.h"

// Pin definitions
const int POWER_PIN_WIN_SERVER = 14; // GPIO14 (D5 on board)
const int POWER_PIN_NAS_SERVER = 5;  // GPIO5  (D1 on board)

PulseAction win_server;
PulseAction nas_server;

void setup()
{
    win_server.init(POWER_PIN_WIN_SERVER, "win-server", mqtt_publish_status);
    nas_server.init(POWER_PIN_NAS_SERVER, "nas-server", mqtt_publish_status);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    Serial.begin(115200);

    // Mount LittleFS
    if (!LittleFS.begin())
    {
        Serial.println("LittleFS mount failed");
        // Optional: format if mount fails. Be careful with this in production.
        // LittleFS.format();
    }
    else
    {
        Serial.println("LittleFS mounted successfully");
    }

    config_store_begin();
    net_begin();

    web_begin();

    mqtt_begin(&win_server, &nas_server);
}

void loop()
{
    net_loop();

    web_loop();
    mqtt_loop();
    win_server.update();
    nas_server.update();
}
