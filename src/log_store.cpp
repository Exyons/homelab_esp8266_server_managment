#include "log_store.h"
#include <stdarg.h>

static LogEntry g_entries[LOG_CAPACITY];
static uint8_t  g_count = 0;    // entries held, up to LOG_CAPACITY
static uint8_t  g_next  = 0;    // next slot to write
static uint32_t g_heap_floor = 0xFFFFFFFF;

// Uptime as something a person can read at a glance rather than a raw
// millisecond count: "3s", "4m 12s", "1h 05m".
static void format_uptime(uint32_t ms, char* out, size_t len)
{
    const uint32_t total = ms / 1000;
    const uint32_t h = total / 3600;
    const uint32_t m = (total % 3600) / 60;
    const uint32_t s = total % 60;
    if (h)      snprintf(out, len, "%luh %02lum", (unsigned long)h, (unsigned long)m);
    else if (m) snprintf(out, len, "%lum %02lus", (unsigned long)m, (unsigned long)s);
    else        snprintf(out, len, "%lus", (unsigned long)s);
}

void log_begin()
{
    g_count = 0;
    g_next  = 0;
    g_heap_floor = ESP.getFreeHeap();
}

void log_add(const char* fmt, ...)
{
    LogEntry& e = g_entries[g_next];
    e.ms = millis();

    va_list args;
    va_start(args, fmt);
    vsnprintf(e.msg, LOG_MSG_LEN, fmt, args);
    va_end(args);

    g_next = (g_next + 1) % LOG_CAPACITY;
    if (g_count < LOG_CAPACITY) g_count++;

    char up[16];
    format_uptime(e.ms, up, sizeof(up));
    Serial.printf("[%s] %s\n", up, e.msg);
}

void log_check_heap()
{
    const uint32_t heap = ESP.getFreeHeap();
    // Only speak up when we drop meaningfully below the previous floor, so a
    // shrinking heap is visible without a line every loop.
    if (heap + 2048 < g_heap_floor) {
        g_heap_floor = heap;
        log_add("Free memory down to %u bytes (largest block %u)",
                heap, ESP.getMaxFreeBlockSize());
    } else if (heap > g_heap_floor) {
        g_heap_floor = heap;   // recovered; re-arm without logging
    }
}

void log_to_json(String& out)
{
    out = "[";
    out.reserve(LOG_CAPACITY * (LOG_MSG_LEN + 32));

    const uint8_t start = (g_count == LOG_CAPACITY) ? g_next : 0;
    for (uint8_t i = 0; i < g_count; i++) {
        const LogEntry& e = g_entries[(start + i) % LOG_CAPACITY];

        char up[16];
        format_uptime(e.ms, up, sizeof(up));

        if (i) out += ',';
        out += "{\"t\":\"";
        out += up;
        out += "\",\"m\":\"";
        // Escape the few characters that would break the JSON string.
        for (const char* p = e.msg; *p; p++) {
            if (*p == '"' || *p == '\\') { out += '\\'; out += *p; }
            else if (*p == '\n')         { out += "\\n"; }
            else if ((uint8_t)*p < 0x20) { /* drop other control chars */ }
            else                          { out += *p; }
        }
        out += "\"}";
    }
    out += ']';
}
