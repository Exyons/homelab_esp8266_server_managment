#pragma once
#include <Arduino.h>

// Small in-RAM ring buffer of human-readable events, surfaced by GET /logs.
//
// Deliberately event-driven rather than periodic: a line is written when
// something actually happens (association, address assigned, broker up or
// down, config saved, update flashed), not on a timer. A heartbeat that
// prints identical numbers every ten seconds tells you nothing and buries
// the one line that mattered.
//
// Statically allocated so it cannot fragment the heap it exists to report on.
// Contents are RAM only and do not survive a reboot.

#define LOG_CAPACITY 24
#define LOG_MSG_LEN  104

struct LogEntry {
    uint32_t ms;                 // millis() when recorded
    char     msg[LOG_MSG_LEN];
};

void log_begin();

// Appends one entry and mirrors it to Serial. Truncates at LOG_MSG_LEN-1.
void log_add(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Notes free heap and emits a line only when it crosses a low-water mark,
// so the log carries a warning when it matters and stays silent otherwise.
void log_check_heap();

// Serialises the buffer, oldest first, into `out` as a JSON array of
// {"t": "<uptime>", "m": "<message>"}.
void log_to_json(String& out);
