#include "node_state.h"
#include "config.h"
#include "debug_log.h"
#include <Arduino.h>

const char* nodeStateName(NodeState s) {
    switch (s) {
        case NodeState::UNSEEN:     return "UNSEEN";
        case NodeState::DISCOVERED: return "DISC";
        case NodeState::ONLINE:     return "ONLINE";
        case NodeState::MISSING:    return "MISSING";
        case NodeState::OFFLINE:    return "OFFLINE";
    }
    return "?";
}

void nodeOnReceive(NodeTelemetry& nt, uint8_t node_id,
                   float angle, float vel, uint8_t status,
                   uint32_t now_ms, uint32_t latency_us,
                   uint32_t cycle_init_retries) {
    const NodeState prev = nt.state;

    nt.angle_rad        = angle;
    nt.velocity_rad_s   = vel;
    nt.status           = status;
    nt.last_rx_us       = micros();
    nt.last_latency_us  = latency_us;
    nt.rx_count++;
    nt.rx_in_window++;
    nt.consecutive_rx++;
    nt.consecutive_miss = 0;

    if (prev == NodeState::UNSEEN) {
        nt.state = NodeState::DISCOVERED;
        nt.first_seen_ms = now_ms;
        nt.init_retries_at_first_seen = (uint16_t)cycle_init_retries;
        dbg_event(Severity::INFO, EvtClass::NODE, node_id,
                  "first reply at t=%lums after %u INIT retries",
                  (unsigned long)now_ms, (unsigned)cycle_init_retries);
        return;
    }

    if (prev == NodeState::OFFLINE) {
        nt.state = NodeState::DISCOVERED;
        nt.consecutive_rx = 1;  // restart stability count
        dbg_event(Severity::INFO, EvtClass::NODE, node_id, "reappeared");
        return;
    }

    if (prev == NodeState::DISCOVERED && nt.consecutive_rx >= NODE_STABLE_CYCLES) {
        nt.state = NodeState::ONLINE;
        dbg_event(Severity::INFO, EvtClass::NODE, node_id, "stable (ONLINE)");
        return;
    }

    if (prev == NodeState::MISSING && nt.consecutive_rx >= NODE_STABLE_CYCLES) {
        nt.state = NodeState::ONLINE;
        dbg_event(Severity::INFO, EvtClass::NODE, node_id, "recovered");
        return;
    }
}

void nodeOnMiss(NodeTelemetry& nt, uint8_t node_id, uint32_t now_ms) {
    (void)now_ms;
    if (nt.state == NodeState::UNSEEN) return;  // Can't miss what never appeared.

    nt.consecutive_miss++;
    nt.consecutive_rx = 0;

    const NodeState prev = nt.state;

    if (prev == NodeState::ONLINE && nt.consecutive_miss >= NODE_MISS_TO_MISSING) {
        nt.state = NodeState::MISSING;
        dbg_event(Severity::WARN, EvtClass::NODE, node_id,
                  "%u consecutive misses", (unsigned)nt.consecutive_miss);
        return;
    }

    if ((prev == NodeState::MISSING || prev == NodeState::DISCOVERED) &&
        nt.consecutive_miss >= NODE_MISS_TO_OFFLINE) {
        nt.state = NodeState::OFFLINE;
        dbg_event(Severity::ERR, EvtClass::NODE, node_id,
                  "OFFLINE - %u consecutive misses", (unsigned)nt.consecutive_miss);
        return;
    }
}
