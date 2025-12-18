#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

// Web updater details
const char *host = "esp8266-webupdate";
const char *update_path = "/firmware";
const char *update_username = "admin";
const char *update_password = "admin";

// Update these with your network details
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

// MQTT Broker details
const char *mqtt_server = MQTT_SERVER;
const int mqtt_port = 8883;
const char *client_id = MQTT_DEVICE_ID; // Must be unique on the broker
const char *mqtt_user = MQTT_USERNAME;
const char *mqtt_password = MQTT_PASSWORD;

// Topics
const char *topic_command = "osiris/esp8266/command";
const char *topic_status = "osiris/esp8266/status";

// Pin definitions
const int POWER_PIN_WIN_SERVER = 14; // GPIO14 (D5 on board)
const int POWER_PIN_NAS_SERVER = 5;  // GPIO5  (D1 on board)

IPAddress BROADCAST_IP = IPAddress(10, 10, 10, 255);
const uint8_t SERVER_MAC[6] = {0xC8, 0xD3, 0xFF, 0x6E, 0x9E, 0xF2};

WiFiUDP udp;
WiFiClientSecure espClient;
PubSubClient client(espClient);
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

unsigned long lastMsg = 0;

void blink_led_fast()
{
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
}

void blink_led_slow()
{
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
    digitalWrite(LED_BUILTIN, HIGH);
}

const int send_magic_packet(const uint16_t port = 7)
{
    uint8_t payload_buffer[102];
    for (int i = 0; i < 6; i += 1)
    {
        payload_buffer[i] = 0xFF;
    }
    for (int i = 6; i < 102; i += 6)
    {
        for (int j = 0; j < 6; j++)
        {
            payload_buffer[i + j] = SERVER_MAC[j];
        }
    }
    udp.beginPacketMulticast(BROADCAST_IP, port, WiFi.localIP());
    udp.write(payload_buffer, sizeof(payload_buffer));
    const int status = udp.endPacket();
    return status;
}

void setup_wifi()
{
    delay(10);
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        digitalWrite(LED_BUILTIN, LOW);
        delay(250);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(250);
    }

    randomSeed(micros());

    Serial.println();
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

void callback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");

    // Convert payload to string for easier comparison
    String message;
    for (unsigned int i = 0; i < length; i++)
    {
        message += (char)payload[i];
    }
    Serial.println(message);

    // Command Handling
    if (String(topic) == topic_command)
    {
        if (message == "PING")
        {
            Serial.println("Ping received. Sending Pong.");
            client.publish(topic_status, "PONG 🏓");
            blink_led_fast();
        }
        else if (message == "FORCE_POWER_OFF_WIN_SERVER")
        {
            client.publish(topic_status, "INFO: Powering off win-server forcefully... 😱");
            digitalWrite(POWER_PIN_WIN_SERVER, LOW);
            delay(5000);
            digitalWrite(POWER_PIN_WIN_SERVER, HIGH);
            client.publish(topic_status, "INFO: Done!! 😃");
        }
        else if (message == "FORCE_POWER_OFF_NAS_SERVER")
        {
            client.publish(topic_status, "INFO: Powering off nas-server forcefully... 😱");
            digitalWrite(POWER_PIN_NAS_SERVER, LOW);
            delay(5000);
            digitalWrite(POWER_PIN_NAS_SERVER, HIGH);
            client.publish(topic_status, "INFO: Done!! 😃");
        }
        else if (message == "POWER_ON_WIN_SERVER")
        {
            client.publish(topic_status, "INFO: Powering on win-server... ☺️");
            digitalWrite(POWER_PIN_WIN_SERVER, LOW);
            delay(500);
            digitalWrite(POWER_PIN_WIN_SERVER, HIGH);
            client.publish(topic_status, "INFO: Done!! 😃");
        }
        else if (message == "POWER_ON_NAS_SERVER")
        {
            client.publish(topic_status, "INFO: Powering on nas-server... ☺️");
            digitalWrite(POWER_PIN_NAS_SERVER, LOW);
            delay(500);
            digitalWrite(POWER_PIN_NAS_SERVER, HIGH);
            client.publish(topic_status, "INFO: Done!! 😃");
        }
        else if (message == "MAGIC_WAKE_NAS")
        {
            const int status = send_magic_packet();
            if (status == 1)
            {
                // Serial.println("Magic Packet Sent to NAS Server");
                client.publish(topic_status, "\\033[0;32mINFO: 🪄 📦 Sent to NAS Server!! ☺️");
            }
            else
            {
                client.publish(topic_status, "ERROR: 🪄 📦 not sent!!! 😢");
            }
            blink_led_fast();
        }
        else
        {
            // Serial.println("Unknown command");
            client.publish(topic_status, "Unknown Command");
        }
    }
}

void reconnect()
{
    // Loop until we're reconnected
    while (!client.connected())
    {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (client.connect(client_id, mqtt_user, mqtt_password))
        {
            Serial.println("connected");
            // Once connected, publish an announcement...
            client.publish(topic_status, "ESP8266 Is Online Bayba Wohoo!!! 😆");
            // ... and resubscribe
            client.subscribe(topic_command);
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

void setup_webupdater()
{
    MDNS.begin(host);

    httpUpdater.setup(&httpServer, update_path, update_username, update_password);
    httpServer.begin();

    MDNS.addService("http", "tcp", 80);
    Serial.printf("HTTPUpdateServer ready! Open http://%s.local%s in your browser and login with username '%s' and password '%s'\n", host, update_path, update_username, update_password);
}

void setup()
{
    pinMode(POWER_PIN_NAS_SERVER, OUTPUT);
    digitalWrite(POWER_PIN_NAS_SERVER, HIGH); // Start with HIGH because for this board HIGH means LOW and vice-versa

    pinMode(POWER_PIN_WIN_SERVER, OUTPUT);
    digitalWrite(POWER_PIN_WIN_SERVER, HIGH);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    Serial.begin(115200);

    setup_wifi();

    setup_webupdater();

    espClient.setInsecure();

    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
}

void loop()
{
    httpServer.handleClient();
    MDNS.update();
    if (!client.connected())
    {
        reconnect();
    }
    client.loop();
}
