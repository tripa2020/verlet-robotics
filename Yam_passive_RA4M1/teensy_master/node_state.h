/*
 * node_state.h — Per-node state machine and telemetry.
 *
 * State machine: UNSEEN -> DISCOVERED -> ONLINE -> MISSING -> OFFLINE
 *   plus MISSING->ONLINE (recovery) and OFFLINE->DISCOVERED (recurrence).
 *
 * Ref: DOCS/Error_handling.md §4
 */

#ifndef NODE_STATE_H
#define NODE_STATE_H

#include <stdint.h>

enum class NodeState : uint8_t {
    UNSEEN     = 0,
    DISCOVERED = 1,
    ONLINE     = 2,
    MISSING    = 3,
    OFFLINE    = 4
};

const char* nodeStateName(NodeState s);

struct NodeTelemetry {
    NodeState state;
    uint32_t  first_seen_ms;
    uint32_t  rx_count;            // lifetime
    uint32_t  rx_in_window;        // resets each 1 Hz tick
    uint32_t  last_rx_us;
    uint32_t  last_latency_us;
    uint16_t  consecutive_rx;
    uint16_t  consecutive_miss;
    uint16_t  init_retries_at_first_seen;
    float     angle_rad;
    float     velocity_rad_s;
    uint8_t   status;
};

// Called when a valid SAMPLE_REPLY arrives. May emit transition events.
// `cycle_init_retries` = total INIT TXs since boot (for first-reply event detail).
void nodeOnReceive(NodeTelemetry& nt, uint8_t node_id,
                   float angle, float vel, uint8_t status,
                   uint32_t now_ms, uint32_t latency_us,
                   uint32_t cycle_init_retries);

// Called once per poll cycle for nodes that did NOT reply. May emit transitions.
void nodeOnMiss(NodeTelemetry& nt, uint8_t node_id, uint32_t now_ms);

#endif // NODE_STATE_H
