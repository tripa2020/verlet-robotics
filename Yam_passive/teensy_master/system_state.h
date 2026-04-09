/*
 * System State - Master State Management
 *
 * NodeState and SystemState structs for tracking all nodes
 * and overall system status.
 *
 * Ref: Serial_master_node_architecture.md Section 5.3
 */

#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdint.h>

constexpr uint8_t MAX_NODES = 6;

//=============================================================================
// Per-Node State
//=============================================================================
struct NodeState {
    uint8_t  node_id;                    // 1-6
    bool     present;                    // discovered and responding

    // Sample data (updated at 100 Hz)
    float    angle;                      // radians (latest from node)
    float    velocity;                   // rad/s (latest from node)
    uint8_t  status_flags;               // from last sample reply

    // Frozen output (used when faulted)
    float    frozen_angle;               // held on fault
    float    frozen_velocity;            // held on fault

    // Timing
    uint32_t last_rx_time_us;            // micros() of last good rx

    // Fault tracking (managed by fault_diagnostics)
    uint16_t consecutive_missed;         // missed poll counter
    bool     comm_faulted;               // true if >= 100 misses

    // Diagnostics (updated at 1 Hz via GET_DIAG)
    uint32_t uptime_ms;
    uint16_t sensor_fail_count;
    uint16_t uart_error_count;           // node-side UART errors
    uint8_t  reset_cause;                // deferred to v2
    uint16_t crc_error_count;
};

//=============================================================================
// Global System State
//=============================================================================
struct SystemState {
    NodeState nodes[MAX_NODES];
    uint8_t   num_configured;            // how many nodes expected
    uint8_t   num_present;               // how many currently responding
    uint8_t   poll_index;                // round-robin index (0 to num_configured-1)
    uint32_t  frame_counter;             // host frames sent
    bool      running;                   // system in RUNNING state
};

//=============================================================================
// State Management Functions
//=============================================================================

// Initialize system state with expected node count
void systemState_init(SystemState& state, uint8_t num_nodes, const uint8_t* node_ids);

// Get node by ID (returns nullptr if not found)
NodeState* systemState_getNode(SystemState& state, uint8_t node_id);

// Update node state from sample reply
void systemState_updateFromSample(SystemState& state, uint8_t node_id,
                                   float angle, float velocity, uint8_t status);

// Update node state from diagnostic reply
void systemState_updateFromDiag(SystemState& state, uint8_t node_id,
                                 uint32_t uptime, uint16_t sensor_fail,
                                 uint16_t uart_err, uint8_t reset_cause,
                                 uint16_t crc_count);

// Mark node as present (responding)
void systemState_markPresent(SystemState& state, uint8_t node_id, uint32_t now_us);

// Recount how many nodes are currently present
void systemState_updatePresenceCount(SystemState& state);

#endif // SYSTEM_STATE_H