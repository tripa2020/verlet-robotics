#include "debug_log.h"
#include "config.h"
#include <stdarg.h>
#include <string.h>

static uint8_t s_level = DEBUG_LEVEL_DEFAULT;

static const char* SEV_STR[] = { "INFO ", "WARN ", "ERR  ", "FATAL" };
static const char* CLS_STR[] = { "BUS",   "TX",    "NODE",  "FRAME", "TIMING", "SYSTEM" };

struct ThrottleEntry { const char* tag; uint32_t last_ms; };
static constexpr uint8_t THROTTLE_SIZE = 8;
static ThrottleEntry s_throttle[THROTTLE_SIZE];

void dbg_init() {
    s_level = DEBUG_LEVEL_DEFAULT;
    for (uint8_t i = 0; i < THROTTLE_SIZE; i++) {
        s_throttle[i].tag = nullptr;
        s_throttle[i].last_ms = 0;
    }
}

void dbg_set_level(uint8_t lvl) {
    if (lvl > 3) lvl = 3;
    s_level = lvl;
}

uint8_t dbg_get_level() { return s_level; }

void dbg_emit_stat_line(const char* line) {
    if (s_level >= 2) Serial.println(line);
}

void dbg_emit_node_line(const char* line) {
    if (s_level >= 2) Serial.println(line);
}

void dbg_event(Severity sev, EvtClass cls, uint8_t node, const char* fmt, ...) {
    const bool always_print = (sev >= Severity::ERR);
    if (s_level < 1 && !always_print) return;

    char msg[112];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    if (node == 0) {
        Serial.printf("EVT  t=%lu sev=%s cls=%s msg=\"%s\"\n",
                      (unsigned long)millis(),
                      SEV_STR[(uint8_t)sev], CLS_STR[(uint8_t)cls], msg);
    } else {
        Serial.printf("EVT  t=%lu sev=%s cls=%s node=%u msg=\"%s\"\n",
                      (unsigned long)millis(),
                      SEV_STR[(uint8_t)sev], CLS_STR[(uint8_t)cls],
                      (unsigned)node, msg);
    }
}

void dbg_reset_events() {
    for (uint8_t i = 0; i < THROTTLE_SIZE; i++) {
        s_throttle[i].tag = nullptr;
        s_throttle[i].last_ms = 0;
    }
}

bool dbg_throttle_ok(const char* tag, uint32_t period_ms) {
    const uint32_t now = millis();
    for (uint8_t i = 0; i < THROTTLE_SIZE; i++) {
        if (s_throttle[i].tag == tag) {
            if (now - s_throttle[i].last_ms >= period_ms) {
                s_throttle[i].last_ms = now;
                return true;
            }
            return false;
        }
    }
    // No existing entry — find an empty slot, or evict the oldest.
    uint8_t target = 0;
    for (uint8_t i = 0; i < THROTTLE_SIZE; i++) {
        if (s_throttle[i].tag == nullptr) { target = i; break; }
        if (s_throttle[i].last_ms < s_throttle[target].last_ms) target = i;
    }
    s_throttle[target].tag = tag;
    s_throttle[target].last_ms = now;
    return true;
}
