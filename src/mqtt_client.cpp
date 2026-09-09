#include "mqtt_client.h"
#include "config_store.h"
#include "net_manager.h"
#include "net_util.h"
#include "version.h"
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>

static WiFiUDP          udp;
static WiFiClientSecure espClient;
static PubSubClient     client(espClient);
static PulseAction*     g_win = nullptr;
static PulseAction*     g_nas = nullptr;

static const unsigned long mqtt_reconnect_interval = 5000;
static unsigned long       mqtt_last_attempt = 0;

static const char* topic_command = "osiris/esp8266/command";
static const char* topic_status  = "osiris/esp8266/status";

static const uint8_t SERVER_MAC[6] = {0xC8, 0xD3, 0xFF, 0x6E, 0x9E, 0xF2};

void mqtt_publish_status(const char* message) {
    if (client.connected()) client.publish(topic_status, message);
}

bool mqtt_connected() { return client.connected(); }

static int send_magic_packet(uint16_t port = 9) {
    uint8_t payload[102];
    for (int i = 0; i < 6; i++) payload[i] = 0xFF;
    for (int i = 6; i < 102; i += 6)
        for (int j = 0; j < 6; j++) payload[i + j] = SERVER_MAC[j];

    // Directed broadcast derived from the live subnet. The previous code used
    // beginPacketMulticast against a hardcoded 10.10.10.255, which is not a
    // multicast address and broke on any other subnet.
    const IPAddress bcast(broadcast_addr((uint32_t)WiFi.localIP(),
                                         (uint32_t)WiFi.subnetMask()));
    udp.beginPacket(bcast, port);
    udp.write(payload, sizeof(payload));
    return udp.endPacket();
}

static void callback(char* topic, byte* payload, unsigned int length) {
    String message;
    message.reserve(length);
    for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
    Serial.printf("[%s] %s\n", topic, message.c_str());

    if (String(topic) != topic_command) return;

    if (message == "AP_MODE_ON") {
        client.publish(topic_status, "Switching to AP mode, hold tight...");
        net_set_ap_forced(true);              // saves and reboots
    } else if (message == "AP_MODE_OFF") {
        client.publish(topic_status, "Leaving AP mode, reconnecting...");
        net_set_ap_forced(false);
    } else if (message == "FACTORY_RESET") {
        client.publish(topic_status, "Wiping config, back to setup mode.");
        net_factory_reset_and_reboot();
    } else if (message == "WIFI_STATUS") {
        String s = String("state=") + (net_is_ap() ? "AP" : "STA") +
                   " ssid=" + (net_is_ap() ? net_ap_ssid() : WiFi.SSID().c_str()) +
                   " rssi=" + String(WiFi.RSSI()) +
                   " ip="   + (net_is_ap() ? WiFi.softAPIP().toString()
                                           : WiFi.localIP().toString());
        client.publish(topic_status, s.c_str());
    }
    else if (message == "PING")
    {
        client.publish(topic_status, "εつ💦(‿ˠ‿) What's good, fam?");
    }
    else if (message == "VERSION")
    {
        String version_info = "We rockin' v" + firmware_version + " right now.";
        client.publish(topic_status, version_info.c_str());
    }
    else if (message == "FORCE_POWER_OFF_WIN_SERVER")
    {
        g_win->trigger(
            5000,
            "(☞ ͡° ͜ʖ ͡°)☞ Aight, I'm finna shut down win-server for real, it's gotta go.",
            "ᕙ(•̀ᗜ•́)ᕗ Win-server is out. It's a wrap",
            "Slow down fam, another message is in flight.");
    }
    else if (message == "FORCE_POWER_OFF_NAS_SERVER")
    {
        g_nas->trigger(
            5000,
            "(☞ ͡° ͜ʖ ͡°)☞ Yo, just heads up, I'm force-killing the nas-server right now.",
            "ᕙ(•̀ᗜ•́)ᕗ Shut down nas-server for real, we good.",
            "One thing at a time, bruh. Wait.");
    }
    else if (message == "POWER_ON_WIN_SERVER")
    {
        g_win->trigger(
            500,
            "(☞ ͡° ͜ʖ ͡°)☞ Bout to fire up win-server... ▄︻デ۪۞━一💥",
            "ᕙ(•̀ᗜ•́)ᕗ Win-server's back in the building. We live!",
            "One thing at a time, bruh. Wait.");
    }
    else if (message == "POWER_ON_NAS_SERVER")
    {
        g_nas->trigger(
            500,
            "(☞ ͡° ͜ʖ ͡°)☞ Bout to get nas-server poppin... ▄︻デ۪۞━一💥",
            "ᕙ(•̀ᗜ•́)ᕗ NAS-server's back in the mix. We rollin'.",
            "Slow down fam, another message is in flight.");
    }
    else if (message == "MAGIC_WAKE_NAS")
    {
        const int status = send_magic_packet();
        if (status == 1)
        {
            client.publish(topic_status, "(-_•)▄︻テحكـ━一💥 Shot that magic packet right into the NAS, it's finna wake up.");
        }
        else
        {
            client.publish(topic_status, "(,,>﹏<,,)👉👈 Nah bruh, that magic packet didn't even go through! 😢");
        }
    }
    else if (message == "FUCK_YOU")
    {
        client.publish(topic_status, "Fuck You 𝓷𝓲𝓰𝓰𝓪𝓪𝓪𝓪...");
        client.publish(topic_status, "⎛⎝(`ᢍ´)⎠⎞ᵐᵘʰᵃʰᵃ");
        client.publish(topic_status, "(-_•)╦̵̵̿╤─");
    }
    else if (message == "MIDDLE_FINGER")
    {
        client.publish(topic_status, "╭∩╮(•̀_·́)╭∩╮");
    }
    else if (message == "DIDDY")
    {
        client.publish(topic_status, "(≖‿≖) Heehee");
        client.publish(topic_status, "𝓓𝓲𝓭𝓭𝔂 𝓽𝓲𝓶𝓮👅🧴🧴");
    }
    else if (message == "BITCH")
    {
        client.publish(topic_status, "(＾◡＾)っ✂╰⋃╯");
    }
    else if (message == "UWU")
    {
        client.publish(topic_status, "U⩊U");
    }
    else if (message == "REBOOT")
    {
        client.publish(topic_status, "Bout to restart, hold tight...");
        delay(500);
        ESP.restart();
    }
    else if (message == "RESET")
    {
        client.publish(topic_status, "Starting fresh, hold your horses.");
        delay(500);
        ESP.reset();
    }
    else
    {
        client.publish(topic_status, "¯\\_(ツ)_/¯ Whatchu mean? I don't know that one.");
    }
}

static void reconnect() {
    Serial.print(F("Attempting MQTT connection..."));
    // Last Will: the broker publishes this if the device drops without a clean
    // disconnect, so the status topic never shows a stale "online".
    const bool ok = client.connect(config().device_id,
                                   config().mqtt_user, config().mqtt_pass,
                                   topic_status, 0, true, "offline");
    if (ok) {
        Serial.printf("connected (heap %u, largest block %u)\n",
                      ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
        // Retained, so a late subscriber sees current state. Paired with the LWT.
        client.publish(topic_status, "online", true);

        // Human-readable announcement. Restored after the module extraction
        // dropped it: this is how the device tells you where to reach its
        // updater, which is the only way to find it on a DHCP lease.
        client.publish(topic_status, "(=^◡^=) Yo Nigga, I'm live! Let's get it!");
        String version_info = "System's at version v" + firmware_version + ", we stayin' current.";
        client.publish(topic_status, version_info.c_str());
        const String local_ip = WiFi.localIP().toString();
        const String update_url_info = "Update server's live! Head to http://" +
                                       String(config().mdns_host) + ".local or http://" +
                                       local_ip + " and lock in with your info.";
        client.publish(topic_status, update_url_info.c_str());

        client.subscribe(topic_command);
    } else {
        Serial.printf(" failed, rc=%d, retry in %.2f s\n",
                      client.state(), mqtt_reconnect_interval / 1000.0);
    }
}

void mqtt_begin(PulseAction* win, PulseAction* nas) {
    g_win = win;
    g_nas = nas;
    espClient.setInsecure();
    // Default TLS buffers are ~16KB each and exhaust the heap alongside the
    // web server. MQTT frames here are far smaller.
    espClient.setBufferSizes(1024, 1024);
    client.setServer(config().mqtt_host, config().mqtt_port);
    client.setCallback(callback);
    // Default PubSubClient buffer is 256 bytes; longer status strings were
    // being dropped silently.
    client.setBufferSize(512);
}

void mqtt_loop() {
    if (net_is_ap()) return;              // no broker while provisioning
    // Also wait for a real station connection. Gating only on !net_is_ap()
    // fired TLS handshakes during NET_STA_CONNECTING with no route, each of
    // which blocks the loop (and so the web server) for seconds before failing
    // with rc=-2.
    if (WiFi.status() != WL_CONNECTED) return;

    // No broker configured: a provisioned-WiFi-but-blank-MQTT device would
    // otherwise spend every cycle trying to resolve "" and blocking the loop,
    // taking the web server down with it.
    if (config().mqtt_host[0] == '\0') {
        static bool warned = false;
        if (!warned) {
            warned = true;
            Serial.println(F("No MQTT host configured; broker connection disabled."));
        }
        return;
    }

    if (millis() - mqtt_last_attempt > mqtt_reconnect_interval && !client.connected()) {
        reconnect();
        // Stamped AFTER the attempt, not before. connect() is a blocking TLS
        // handshake that can outlast the interval; stamping first meant the
        // guard had already expired by the next iteration, so attempts ran
        // back to back with no backoff and starved http_server.handleClient().
        mqtt_last_attempt = millis();
    }
    client.loop();
}
