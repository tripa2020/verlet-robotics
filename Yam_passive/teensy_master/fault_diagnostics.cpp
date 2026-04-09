/*
 * Fault Diagnostics Implementation
 *
 * Ref: Serial_master_node_architecture.md Section 5.3
 */

#include "fault_diagnostics.h"
#include <protocol.h>

//=============================================================================
// Timeout Detection
//=============================================================================
bool faultDiag_checkTimeout(const NodeState& node, uint32_t now_us) {
    // Handle micros() wrap-around
    uint32_t elapsed = now_us - node.last_rx_time_us;
    return elapsed > REPLY_TIMEOUT_US;
}

//=============================================================================
// Miss Counting
//=============================================================================
void faultDiag_incrementMissed(NodeState& node) {
    if (node.consecutive_missed < 0xFFFF) {
        node.consecutive_missed++;
    }

    // Check fault threshold
    if (node.consecutive_missed >= COMM_FAULT_THRESHOLD) {
        if (!node.comm_faulted) {
            // Entering fault state - freeze current values
            faultDiag_freezeOutput(node);
            node.comm_faulted = true;
        }
    }
}

void faultDiag_clearMissed(NodeState& node) {
    node.consecutive_missed = 0;
}

//=============================================================================
// Fault State Queries
//=============================================================================
bool faultDiag_isCommFaulted(const NodeState& node) {
    return node.comm_faulted;
}

bool faultDiag_isSensorFaulted(const NodeState& node) {
    return statusIsFaulted(node.status_flags);
}

bool faultDiag_shouldFreeze(const NodeState& node) {
    return node.comm_faulted || statusIsFaulted(node.status_flags);
}

//=============================================================================
// Freeze Management
//=============================================================================
void faultDiag_freezeOutput(NodeState& node) {
    node.frozen_angle = node.angle;
    node.frozen_velocity = node.velocity;
}

void faultDiag_getOutput(const NodeState& node, float& angle, float& velocity, uint8_t& status) {
    if (faultDiag_shouldFreeze(node)) {
        // Return frozen values
        angle = node.frozen_angle;
        velocity = node.frozen_velocity;
        // Add STALE flag to indicate frozen data
        status = node.status_flags | STATUS_STALE;
    } else {
        // Return live values
        angle = node.angle;
        velocity = node.velocity;
        status = node.status_flags;
    }
}

//=============================================================================
// Fault Recovery
//=============================================================================
void faultDiag_clearCommFault(NodeState& node) {
    node.comm_faulted = false;
    node.consecutive_missed = 0;
}
