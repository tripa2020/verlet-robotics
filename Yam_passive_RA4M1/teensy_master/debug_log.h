/*
 * debug_log.h — Severity-tagged event emitter for Teensy master.
 *
 * Emits ASCII key=value lines on Serial. Format spec: DOCS/Error_handling.md §5.
 *
 * STAT and NODE lines honor the verbosity level (gated at level >= 2).
 * EVT lines honor level >= 1, except severity >= ERR which always prints.
 */

#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <Arduino.h>
#include <stdint.h>

enum class Severity : uint8_t { INFO = 0, WARN = 1, ERR = 2, FATAL = 3 };
enum class EvtClass : uint8_t {
    BUS = 0, TX = 1, NODE = 2, FRAME = 3, TIMING = 4, SYSTEM = 5
};

void dbg_init();

void dbg_set_level(uint8_t lvl);   // 0..3
uint8_t dbg_get_level();

void dbg_emit_stat_line(const char* line);
void dbg_emit_node_line(const char* line);

// Emit one EVT line. node=0 means "no node-specific context".
void dbg_event(Severity sev, EvtClass cls, uint8_t node, const char* fmt, ...);

// Clears throttle table so repeated events can fire immediately again.
void dbg_reset_events();

// Returns true at most once per `period_ms` per unique tag pointer.
// Pass a string literal as `tag` so pointer identity is stable.
bool dbg_throttle_ok(const char* tag, uint32_t period_ms);

#endif // DEBUG_LOG_H
