#include "pulse_action.h"
#include <Arduino.h>

void PulseAction::init(unsigned int _pin, const char* _name, PulseStatusFn _publish) {
    pin      = _pin;
    name     = _name;
    publish  = _publish;
    timer    = 0;
    active   = false;
    duration = 0;
    update_message = nullptr;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);   // off
}

void PulseAction::trigger(unsigned long ms, const char* init_message,
                          const char* _update_message, const char* active_message) {
    if (active) {
        if (publish) publish(active_message);
        return;
    }
    if (publish) publish(init_message);
    digitalWrite(pin, LOW);    // on
    timer          = millis();
    duration       = ms;
    active         = true;
    update_message = _update_message;
}

void PulseAction::update() {
    if (active && (millis() - timer) >= duration) {
        digitalWrite(pin, HIGH);   // off
        active = false;
        if (publish && update_message) publish(update_message);
    }
}
