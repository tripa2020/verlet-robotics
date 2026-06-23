/*
 * host_control.h — Serial line parser for Python -> master control commands.
 *
 * Commands: SET level=N, RESET counters, RESET events, PING, DUMP config.
 * Format spec: DOCS/Error_handling.md §5.2
 */

#ifndef HOST_CONTROL_H
#define HOST_CONTROL_H

#include <stdint.h>

void host_ctrl_init();

// Master registers a callback for "RESET counters" — it owns the counters.
typedef void (*ResetCountersCb)();
void host_ctrl_set_reset_counters_cb(ResetCountersCb cb);

// Call from loop(). Drains Serial.available(), parses complete lines, dispatches.
void host_ctrl_poll();

#endif // HOST_CONTROL_H
