#pragma once

// Publishes a status string. Injected so this module does not depend on MQTT.
typedef void (*PulseStatusFn)(const char* message);

struct PulseAction {
    unsigned int  pin;
    unsigned long timer;
    unsigned long duration;
    bool          active;
    const char*   name;
    const char*   update_message;
    PulseStatusFn publish;

    // Board logic is inverted: HIGH means off.
    void init(unsigned int _pin, const char* _name, PulseStatusFn _publish);
    void trigger(unsigned long ms, const char* init_message,
                 const char* _update_message, const char* active_message);
    void update();
};
