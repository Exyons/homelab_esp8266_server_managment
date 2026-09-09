#pragma once

void web_begin();
void web_loop();

// Sends a 401 and returns false when the request is unauthenticated.
// Credentials come from the config, falling back to admin/admin when the
// stored config is invalid.
bool web_require_auth();

// Starts (or restarts) the mDNS responder and re-advertises the HTTP service.
// Must be called only once the station has an IP: ESP8266mDNS binds to the
// active interface, so starting it while the station is still associating
// leaves the responder dead and <hostname>.local unresolvable.
void web_start_mdns();
