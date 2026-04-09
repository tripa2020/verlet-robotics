/*
 * Node Manager - Poll Sequencing and Orchestration
 *
 * Manages polling sequence, reply processing, host frame building,
 * and output policy (freeze logic).
 *
 * Ref: Serial_master_node_architecture.md Section 5.3 (node_manager.h)
 */

#ifndef NODE_MANAGER_H
#define NODE_MANAGER_H

#include "system_state.h"
#include "serial_interface.h"
#include "fault_diagnostics.h"
#include <protocol.h>

class NodeManager {
public:
    // Dependency injection
    void init(SystemState& state, INodeBus& bus, IHostStream& host);

    //=========================================================================
    // Poll Sequencing
    //=========================================================================

    // Get current node ID to poll (1-6)
    uint8_t getCurrentNodeId();

    // Advance to next node in round-robin sequence
    void advancePollIndex();

    //=========================================================================
    // Bus Operations
    //=========================================================================

    // Poll a specific node with a command
    // Returns true if request was sent
    bool pollNode(uint8_t node_id, uint8_t cmd);

    // Process reply for a node
    // Returns true if valid reply received
    bool processReply(uint8_t node_id, uint8_t expected_cmd);

    //=========================================================================
    // Host Stream
    //=========================================================================

    // Build and send host frame with all node data
    void sendHostFrame();

    //=========================================================================
    // Output Policy
    //=========================================================================

    // Get output values for a node (frozen if faulted, live if healthy)
    void getNodeOutput(uint8_t node_id, float& angle, float& velocity, uint8_t& status);

    //=========================================================================
    // Diagnostics
    //=========================================================================

    // Get poll statistics
    uint32_t getPollCount() const { return m_poll_count; }
    uint32_t getSuccessCount() const { return m_success_count; }
    uint32_t getTimeoutCount() const { return m_timeout_count; }
    uint32_t getCrcErrorCount() const { return m_crc_error_count; }

private:
    SystemState*  m_state = nullptr;
    INodeBus*     m_bus   = nullptr;
    IHostStream*  m_host  = nullptr;

    // Frame buffers
    uint8_t m_tx_buffer[16];
    uint8_t m_rx_buffer[16];

    // Host frame buffer (max 67 bytes for 6 nodes)
    uint8_t m_host_buffer[72];

    // Statistics
    uint32_t m_poll_count = 0;
    uint32_t m_success_count = 0;
    uint32_t m_timeout_count = 0;
    uint32_t m_crc_error_count = 0;

    // Internal helpers
    void buildRequestFrame(uint8_t node_id, uint8_t cmd);
    bool parseSampleReply(uint8_t node_id, size_t len);
    bool parseDiagReply(uint8_t node_id, size_t len);
    bool parsePingReply(uint8_t node_id, size_t len);
    void buildHostFrame(size_t& len);
};

#endif // NODE_MANAGER_H
