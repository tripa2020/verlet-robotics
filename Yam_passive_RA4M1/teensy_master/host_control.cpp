#include "host_control.h"
#include "debug_log.h"
#include "config.h"
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

static char            s_line[HOST_CMD_LINE_MAX];
static uint8_t         s_idx = 0;
static ResetCountersCb s_reset_counters_cb = nullptr;

void host_ctrl_init() {
    s_idx = 0;
    s_line[0] = '\0';
    s_reset_counters_cb = nullptr;
}

void host_ctrl_set_reset_counters_cb(ResetCountersCb cb) {
    s_reset_counters_cb = cb;
}

static void trim_trailing(char* s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t')) {
        s[--n] = '\0';
    }
}

static void dispatch(char* line) {
    trim_trailing(line);
    if (line[0] == '\0') return;

    if (strncmp(line, "SET level=", 10) == 0) {
        const int lvl = atoi(line + 10);
        if (lvl < 0 || lvl > 3) {
            Serial.printf("NAK cmd=\"%s\" reason=\"level out of range\"\n", line);
            return;
        }
        dbg_set_level((uint8_t)lvl);
        Serial.printf("ACK level=%d\n", lvl);
    }
    else if (strcmp(line, "RESET counters") == 0) {
        if (s_reset_counters_cb) s_reset_counters_cb();
        Serial.println("ACK reset=counters");
    }
    else if (strcmp(line, "RESET events") == 0) {
        dbg_reset_events();
        Serial.println("ACK reset=events");
    }
    else if (strcmp(line, "PING") == 0) {
        Serial.printf("PONG t=%lu\n", (unsigned long)millis());
    }
    else if (strcmp(line, "DUMP config") == 0) {
        Serial.printf("CFG num_nodes=%u\n",          (unsigned)NUM_NODES);
        Serial.printf("CFG poll_period_ms=%lu\n",    (unsigned long)POLL_PERIOD_MS);
        Serial.printf("CFG reply_timeout_us=%lu\n",  (unsigned long)REPLY_TIMEOUT_US);
        Serial.printf("CFG init_retry_ms=%lu\n",     (unsigned long)INIT_RETRY_MS);
        Serial.printf("CFG can_baud=%lu\n",          (unsigned long)CAN_BAUD);
        Serial.printf("CFG debug_level=%u\n",        (unsigned)dbg_get_level());
        Serial.println("ACK dump=config");
    }
    else {
        Serial.printf("NAK cmd=\"%s\" reason=\"unknown command\"\n", line);
    }
}

void host_ctrl_poll() {
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c < 0) break;
        if (c == '\n' || c == '\r') {
            if (s_idx > 0) {
                s_line[s_idx] = '\0';
                dispatch(s_line);
                s_idx = 0;
            }
        } else if (s_idx < HOST_CMD_LINE_MAX - 1) {
            s_line[s_idx++] = (char)c;
        } else {
            s_line[HOST_CMD_LINE_MAX - 1] = '\0';
            Serial.printf("NAK cmd=\"%s...\" reason=\"line overflow\"\n", s_line);
            s_idx = 0;
        }
    }
}
