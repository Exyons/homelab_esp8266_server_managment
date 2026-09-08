#include "net_decide.h"

NetState net_initial_state(bool provisioned, bool ap_forced, bool reset_requested) {
    if (reset_requested) return NET_AP;
    if (ap_forced)       return NET_AP;
    if (!provisioned)    return NET_AP;
    return NET_STA_CONNECTING;
}
