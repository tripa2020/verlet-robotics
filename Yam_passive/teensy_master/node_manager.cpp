/*
 * Node Manager Implementation
 *
 * Ref: Serial_master_node_architecture.md Section 5.3
 */

#include "node_manager.h"
#include "config.h"
#include <string.h>

//=============================================================================
// Initialization
//=============================================================================
void NodeManager::init(SystemState& state, INodeBus& bus, IHostStream& host) {
    m_state = &state;
    m_bus = &bus;
    m_host = &host;

    m_poll_count = 0;
    m_success_count = 0;
    m_timeout_count = 0;
    m_crc_error_count = 0;
}

//=============================================================================
// Poll Sequencing
//=============================================================================
uint8_t NodeManager::getCurrentNodeId() {
    if (m_state == nullptr || m_state->num_configured == 0) return 0;
    return m_state->nodes[m_state->poll_index].node_id;
}

void NodeManager::advancePollIndex() {
    if (m_state == nullptr || m_state->num_configured == 0) return;

    m_state->poll_index++;
    if (m_state->poll_index >= m_state->num_configured) {
        m_state->poll_index = 0;
    }
}

//=============================================================================
// Bus Operations
//=============================================================================
void NodeManager::buildRequestFrame(uint8_t node_id, uint8_t cmd) {
    m_tx_buffer[0] = FRAME_START_REQUEST;
    m_tx_buffer[1] = node_id;
    m_tx_buffer[2] = cmd;
    m_tx_buffer[3] = crc8(m_tx_buffer, 3);
}

bool NodeManager::pollNode(uint8_t node_id, uint8_t cmd) {
    if (m_bus == nullptr) return false;

    // Flush any stale RX data
    m_bus->flushRx(node_id);

    // Build and send request
    buildRequestFrame(node_id, cmd);
    m_bus->sendRequest(node_id, m_tx_buffer, FRAME_SIZE_REQUEST);
    m_poll_count++;

    return true;
}

bool NodeManager::processReply(uint8_t node_id, uint8_t expected_cmd) {
    if (m_bus == nullptr || m_state == nullptr) return false;

    // Determine expected reply size
    size_t expected_len;
    switch (expected_cmd) {
        case CMD_GET_SAMPLE:
            expected_len = FRAME_SIZE_SAMPLE;
            break;
        case CMD_GET_DIAG:
            expected_len = FRAME_SIZE_DIAGNOSTIC;
            break;
        case CMD_PING:
            expected_len = FRAME_SIZE_PING;
            break;
        default:
            return false;
    }

    // Read reply with timeout
    size_t len = m_bus->readReply(node_id, m_rx_buffer, expected_len, REPLY_TIMEOUT_US);

    if (len < expected_len) {
        // Timeout - increment missed counter
        NodeState* node = systemState_getNode(*m_state, node_id);
        if (node != nullptr) {
            faultDiag_incrementMissed(*node);
        }
        m_timeout_count++;
        return false;
    }

    // Parse based on command type
    bool valid = false;
    switch (expected_cmd) {
        case CMD_GET_SAMPLE:
            valid = parseSampleReply(node_id, len);
            break;
        case CMD_GET_DIAG:
            valid = parseDiagReply(node_id, len);
            break;
        case CMD_PING:
            valid = parsePingReply(node_id, len);
            break;
    }

    if (valid) {
        m_success_count++;
        NodeState* node = systemState_getNode(*m_state, node_id);
        if (node != nullptr) {
            faultDiag_clearMissed(*node);
            systemState_markPresent(*m_state, node_id, micros());

            // Clear comm fault if we were faulted and now receiving
            if (node->comm_faulted) {
                faultDiag_clearCommFault(*node);
            }
        }
    }

    return valid;
}

//=============================================================================
// Reply Parsing
//=============================================================================
bool NodeManager::parseSampleReply(uint8_t node_id, size_t len) {
    if (len != FRAME_SIZE_SAMPLE) return false;

    // Check start byte
    if (m_rx_buffer[0] != FRAME_START_SAMPLE) return false;

    // Check node ID
    if (m_rx_buffer[1] != node_id) return false;

    // Verify CRC
    uint8_t calc_crc = crc8(m_rx_buffer, 11);
    if (calc_crc != m_rx_buffer[11]) {
        m_crc_error_count++;
        return false;
    }

    // Parse data (little-endian)
    float angle, velocity;
    memcpy(&angle, &m_rx_buffer[2], 4);
    memcpy(&velocity, &m_rx_buffer[6], 4);
    uint8_t status = m_rx_buffer[10];

    // Update state
    systemState_updateFromSample(*m_state, node_id, angle, velocity, status);

    return true;
}

bool NodeManager::parseDiagReply(uint8_t node_id, size_t len) {
    if (len != FRAME_SIZE_DIAGNOSTIC) return false;

    // Check start byte
    if (m_rx_buffer[0] != FRAME_START_DIAGNOSTIC) return false;

    // Check node ID
    if (m_rx_buffer[1] != node_id) return false;

    // Verify CRC
    uint8_t calc_crc = crc8(m_rx_buffer, 14);
    if (calc_crc != m_rx_buffer[14]) {
        m_crc_error_count++;
        return false;
    }

    // Parse data (little-endian)
    uint32_t uptime;
    uint16_t sensor_fail, uart_err, crc_count;
    memcpy(&uptime, &m_rx_buffer[2], 4);
    memcpy(&sensor_fail, &m_rx_buffer[6], 2);
    memcpy(&uart_err, &m_rx_buffer[8], 2);
    uint8_t reset_cause = m_rx_buffer[10];
    memcpy(&crc_count, &m_rx_buffer[11], 2);

    // Update state
    systemState_updateFromDiag(*m_state, node_id, uptime, sensor_fail,
                                uart_err, reset_cause, crc_count);

    return true;
}

bool NodeManager::parsePingReply(uint8_t node_id, size_t len) {
    if (len != FRAME_SIZE_PING) return false;

    // Check start byte
    if (m_rx_buffer[0] != FRAME_START_PING) return false;

    // Check node ID
    if (m_rx_buffer[1] != node_id) return false;

    // Verify CRC
    uint8_t calc_crc = crc8(m_rx_buffer, 3);
    if (calc_crc != m_rx_buffer[3]) {
        m_crc_error_count++;
        return false;
    }

    // Ping reply only has status byte
    uint8_t status = m_rx_buffer[2];

    // Update node status flags
    NodeState* node = systemState_getNode(*m_state, node_id);
    if (node != nullptr) {
        node->status_flags = status;
    }

    return true;
}

//=============================================================================
// Host Stream
//=============================================================================
void NodeManager::buildHostFrame(size_t& len) {
    if (m_state == nullptr) {
        len = 0;
        return;
    }

    uint8_t* p = m_host_buffer;

    // Header
    *p++ = FRAME_START_HOST;

    // Timestamp (uint32 LE)
    uint32_t timestamp = micros();
    memcpy(p, &timestamp, 4);
    p += 4;

    // Number of nodes
    *p++ = m_state->num_configured;

    // Per-node data blocks
    for (uint8_t i = 0; i < m_state->num_configured; i++) {
        NodeState& node = m_state->nodes[i];

        // Get output (frozen if faulted)
        float angle, velocity;
        uint8_t status;
        faultDiag_getOutput(node, angle, velocity, status);

        // Node ID
        *p++ = node.node_id;

        // Angle (float32 LE)
        memcpy(p, &angle, 4);
        p += 4;

        // Velocity (float32 LE)
        memcpy(p, &velocity, 4);
        p += 4;

        // Status
        *p++ = status;
    }

    // Calculate frame length so far (without CRC)
    size_t frame_len = p - m_host_buffer;

    // CRC over all preceding bytes
    *p++ = crc8(m_host_buffer, frame_len);

    len = p - m_host_buffer;
}

void NodeManager::sendHostFrame() {
    if (m_host == nullptr) return;

    size_t len;
    buildHostFrame(len);

    if (len > 0) {
        m_host->sendFrame(m_host_buffer, len);
        m_state->frame_counter++;
    }
}

//=============================================================================
// Output Policy
//=============================================================================
void NodeManager::getNodeOutput(uint8_t node_id, float& angle, float& velocity, uint8_t& status) {
    if (m_state == nullptr) {
        angle = 0.0f;
        velocity = 0.0f;
        status = 0;
        return;
    }

    NodeState* node = systemState_getNode(*m_state, node_id);
    if (node == nullptr) {
        angle = 0.0f;
        velocity = 0.0f;
        status = 0;
        return;
    }

    faultDiag_getOutput(*node, angle, velocity, status);
}
