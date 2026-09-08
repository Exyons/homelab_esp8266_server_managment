#pragma once

enum NetState { NET_STA_CONNECTING = 0, NET_AP = 1 };

// Which network state to enter at boot. AP wins whenever the device cannot or
// should not attempt a station connection.
NetState net_initial_state(bool provisioned, bool ap_forced, bool reset_requested);
