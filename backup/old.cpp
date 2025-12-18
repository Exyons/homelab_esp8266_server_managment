#include <Arduino.h>
#include <ESP8266WiFiMulti.h>
#include <WiFiUdp.h>

ESP8266WiFiMulti wifi_multi;
WiFiUDP udp;

int ledstate = HIGH;
unsigned int prev_millis = 0;
const int led_1_pin = LED_BUILTIN; // Pin 2
const unsigned int small_interval = 50;
const unsigned int long_interval = 500;
const uint32_t connection_timeout_ms = 5000;

IPAddress BROADCAST_IP = IPAddress(10, 10, 10, 255);
const uint8_t SERVER_MAC[6] = {0xC8, 0xD3, 0xFF, 0x6E, 0x9E, 0xF2};

void blink_millis(const unsigned int);
void blink_delay(const bool, const uint8, const unsigned int);
void send_magic_packet(const uint16_t=7);

void setup()
{
    // put your setup code here, to run once:
    Serial.begin(9600);
    Serial.println();
    pinMode(led_1_pin, OUTPUT);
    // pinMode(1, OUTPUT); // Use other GPIO and not Pins 1 & 3. These two are required for serial communication.

    WiFi.mode(WIFI_STA);
    wifi_multi.addAP(WIFI_SSID_1, WIFI_PASSWORD_1);
    wifi_multi.addAP(WIFI_SSID_2, WIFI_PASSWORD_2);
    Serial.println("Connecting...");
    while (wifi_multi.run(connection_timeout_ms) != WL_CONNECTED)
    {
        blink_delay(false, 5, long_interval);
        Serial.print(".");
        // delay(500);
    }

    blink_delay(true, 5, small_interval);

    Serial.println();
    Serial.print("Connected, IP Address: ");
    Serial.print(WiFi.localIP());
    WiFi.printDiag(Serial);

    // send_magic_packet();

    // digitalWrite(1, LOW);
    // delay(500);
    // digitalWrite(1, HIGH);
}

void loop()
{
    // Serial.println(analogRead(1));
}

void blink_millis(const unsigned int interval)
{
    // For this chip LOW means turn `on` and HIGH means turn `off`
    unsigned int current_millis = millis();
    if (current_millis - prev_millis >= interval)
    {
        prev_millis = current_millis;
        if (ledstate == LOW)
        {
            ledstate = HIGH;
        }
        else
        {
            ledstate = LOW;
        }
        digitalWrite(led_1_pin, ledstate);
    }
}

void blink_delay(const bool loop, const uint8 i = 5, const unsigned int interval = 500)
{
    // For this chip LOW means turn `on` and HIGH means turn `off`
    if (loop)
    {
        for (uint8 _i = 0; _i < i; _i++)
        {
            digitalWrite(led_1_pin, LOW);
            delay(interval);
            digitalWrite(led_1_pin, HIGH);
            delay(interval);
        }
    }
    else
    {
        digitalWrite(led_1_pin, LOW);
        delay(interval);
        digitalWrite(led_1_pin, HIGH);
        delay(interval);
    }
}

// You don not define the default value again in function declaration
void send_magic_packet(const uint16_t port)
{
    // Initialize magic packet payload
    // Magic packet is always 102 bits
    uint8_t payload_buffer[102];

    // First 6 bytes are 0xFF
    for (int i = 0; i < 6; i += 1)
    {
        payload_buffer[i] = 0xFF;
    }

    // Repeat MAC address 16 times
    for (int i = 6; i < 102; i += 6)
    {
        for (int j = 0; j < 6; j++)
        {
            payload_buffer[i + j] = SERVER_MAC[j];
        }
    }
    // Printing out payload to serial
    // for (size_t i = 0; i < sizeof(payload_buffer); i++)
    // {
    //     Serial.print((int)payload_buffer[i], HEX);
    //     Serial.print(" ");
    // }
    Serial.println();

    // Sending magic packet
    udp.beginPacketMulticast(BROADCAST_IP, port, WiFi.localIP());
    udp.write(payload_buffer, sizeof(payload_buffer));
    const int status = udp.endPacket();

    Serial.println();
    Serial.print("Status: ");
    Serial.print(status);
}