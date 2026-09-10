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

// Severity is decided where the event is raised — the call site knows whether
// something is routine, degraded, or broken. Inferring it later from keywords
// in the message text guesses, and guesses wrong.
enum LogLevel : uint8_t {
    LOG_INFO  = 0,   // normal progress
    LOG_WARN  = 1,   // degraded but self-recovering
    LOG_ERROR = 2    // failed; needs attention
};

struct LogEntry {
    uint32_t ms;                 // millis() when recorded
    uint8_t  level;              // LogLevel
    char     msg[LOG_MSG_LEN];
};

void log_begin();

// Append one entry at the given severity and mirror it to Serial.
// Truncates at LOG_MSG_LEN-1.
void log_info (const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void log_warn (const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void log_error(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Notes free heap and emits a line only when it crosses a low-water mark,
// so the log carries a warning when it matters and stays silent otherwise.
void log_check_heap();

// Serialises the buffer, oldest first, into `out` as a JSON array of
// {"t": "<uptime>", "l": "info|warn|error", "m": "<message>"}.
void log_to_json(String& out);
