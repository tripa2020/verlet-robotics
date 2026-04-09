/*
 * System State Implementation
 *
 * Ref: Serial_master_node_architecture.md Section 5.3
 */

#include "system_state.h"
#include <string.h>

//=============================================================================
// Initialize
//=============================================================================
void systemState_init(SystemState& state, uint8_t num_nodes, const uint8_t* node_ids) {
    memset(&state, 0, sizeof(SystemState));

    state.num_configured = (num_nodes <= MAX_NODES) ? num_nodes : MAX_NODES;
    state.num_present = 0;
    state.poll_index = 0;
    state.frame_counter = 0;
    state.running = false;

    // Initialize configured nodes
    for (uint8_t i = 0; i < state.num_configured; i++) {
        state.nodes[i].node_id = node_ids[i];
        state.nodes[i].present = false;
        state.nodes[i].angle = 0.0f;
        state.nodes[i].velocity = 0.0f;
        state.nodes[i].status_flags = 0;
        state.nodes[i].frozen_angle = 0.0f;
        state.nodes[i].frozen_velocity = 0.0f;
        state.nodes[i].last_rx_time_us = 0;
        state.nodes[i].consecutive_missed = 0;
        state.nodes[i].comm_faulted = false;
        state.nodes[i].uptime_ms = 0;
        state.nodes[i].sensor_fail_count = 0;
        state.nodes[i].uart_error_count = 0;
        state.nodes[i].reset_cause = 0x03;  // RESET_UNKNOWN
        state.nodes[i].crc_error_count = 0;
    }
}

//=============================================================================
// Get Node by ID
//=============================================================================
NodeState* systemState_getNode(SystemState& state, uint8_t node_id) {
    for (uint8_t i = 0; i < state.num_configured; i++) {
        if (state.nodes[i].node_id == node_id) {
            return &state.nodes[i];
        }
    }
    return nullptr;
}

//=============================================================================
// Update from Sample Reply
//=============================================================================
void systemState_updateFromSample(SystemState& state, uint8_t node_id,
                                   float angle, float velocity, uint8_t status) {
    NodeState* node = systemState_getNode(state, node_id);
    if (node == nullptr) return;

    node->angle = angle;
    node->velocity = velocity;
    node->status_flags = status;
}

//=============================================================================
// Update from Diagnostic Reply
//=============================================================================
void systemState_updateFromDiag(SystemState& state, uint8_t node_id,
                                 uint32_t uptime, uint16_t sensor_fail,
                                 uint16_t uart_err, uint8_t reset_cause,
                                 uint16_t crc_count) {
    NodeState* node = systemState_getNode(state, node_id);
    if (node == nullptr) return;

    node->uptime_ms = uptime;
    node->sensor_fail_count = sensor_fail;
    node->uart_error_count = uart_err;
    node->reset_cause = reset_cause;
    node->crc_error_count = crc_count;
}

//=============================================================================
// Mark Node Present
//=============================================================================
void systemState_markPresent(SystemState& state, uint8_t node_id, uint32_t now_us) {
    NodeState* node = systemState_getNode(state, node_id);
    if (node == nullptr) return;

    node->present = true;
    node->last_rx_time_us = now_us;
}

//=============================================================================
// Update Presence Count
//=============================================================================
void systemState_updatePresenceCount(SystemState& state) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < state.num_configured; i++) {
        if (state.nodes[i].present && !state.nodes[i].comm_faulted) {
            count++;
        }
    }
    state.num_present = count;
}