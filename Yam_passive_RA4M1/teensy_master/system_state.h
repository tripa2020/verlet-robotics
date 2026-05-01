/*
 * System State — Teensy 4.1 Master
 *
 * NodeState and SystemState structures for tracking encoder network.
 * Stub file for Milestone 0 — implementation in Milestone 5.
 *
 * Ref: DOCS/Architecture.md Section 5
 */

#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdint.h>
#include "config.h"

// TODO (Milestone 5): Implement state structures

/*
struct NodeState {
    uint8_t  node_id;
    bool     present;
    uint16_t angle_raw;
    int16_t  velocity_raw;
    int16_t  accel_raw;
    uint8_t  status_flags;
    uint8_t  seq_num;
    uint32_t last_rx_time_us;
    uint16_t consecutive_missed;
    bool     comm_faulted;
};

struct SystemState {
    NodeState nodes[NUM_NODES];
    uint8_t   num_present;
    uint32_t  frame_counter;
    bool      running;
};
*/

#endif // SYSTEM_STATE_H
