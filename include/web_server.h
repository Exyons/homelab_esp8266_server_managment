#pragma once

void web_begin();
void web_loop();

// Sends a 401 and returns false when the request is unauthenticated.
// Credentials come from the config, falling back to admin/admin when the
// stored config is invalid.
bool web_require_auth();
