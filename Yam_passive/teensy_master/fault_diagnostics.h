/*
 * Fault Diagnostics - Timeout and Fault Management
 *
 * Handles communication timeout detection, miss counting,
 * fault state management, and output freeze logic.
 *
 * Ref: Serial_master_node_architecture.md Section 5.3 (fault_diagnostics.h)
 */

#ifndef FAULT_DIAGNOSTICS_H
#define FAULT_DIAGNOSTICS_H

#include "system_state.h"
#include "config.h"

//=============================================================================
// Timeout Detection
//=============================================================================

// Check if node reply has timed out
// Returns true if (now_us - last_rx_time_us) > REPLY_TIMEOUT_US
bool faultDiag_checkTimeout(const NodeState& node, uint32_t now_us);

//=============================================================================
// Miss Counting
//=============================================================================

// Increment missed poll counter, may trigger fault
void faultDiag_incrementMissed(NodeState& node);

// Clear missed counter (called on successful reply)
void faultDiag_clearMissed(NodeState& node);

//=============================================================================
// Fault State Queries
//=============================================================================

// Check if node has communication fault (>= COMM_FAULT_THRESHOLD misses)
bool faultDiag_isCommFaulted(const NodeState& node);

// Check if node has sensor fault (from status flags)
bool faultDiag_isSensorFaulted(const NodeState& node);

// Check if output should be frozen (comm OR sensor fault)
bool faultDiag_shouldFreeze(const NodeState& node);

//=============================================================================
// Freeze Management
//=============================================================================

// Copy current values to frozen values
void faultDiag_freezeOutput(NodeState& node);

// Get output values (returns frozen if faulted, live if healthy)
void faultDiag_getOutput(const NodeState& node, float& angle, float& velocity, uint8_t& status);

//=============================================================================
// Fault Recovery
//=============================================================================

// Clear comm fault (called when node starts responding again)
void faultDiag_clearCommFault(NodeState& node);

#endif // FAULT_DIAGNOSTICS_H