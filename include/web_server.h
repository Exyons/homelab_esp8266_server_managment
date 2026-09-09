#pragma once

void web_begin();
void web_loop();

// Sends a 401 and returns false when the request is unauthenticated.
// Credentials come from the config, falling back to admin/admin when the
// stored config is invalid.
bool web_require_auth();

// Re-binds the HTTP listener and starts the mDNS responder. Must be called
// only once the interface has an IP. Both bind to the active interface, so
// starting them while the station is still associating leaves the listener
// not accepting and <hostname>.local unresolvable. net_manager calls this on
// the transition to NET_STA_CONNECTED.
void web_on_network_up();
