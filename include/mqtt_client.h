#pragma once
#include "pulse_action.h"

void mqtt_begin(PulseAction* win, PulseAction* nas);
void mqtt_loop();
void mqtt_publish_status(const char* message);
bool mqtt_connected();
