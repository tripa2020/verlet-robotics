/*
 * Serial Interface Implementation
 *
 * Ref: Serial_master_node_architecture.md Section 5.3
 */

#include "serial_interface.h"

//=============================================================================
// MultiUartBus Implementation
//=============================================================================

void MultiUartBus::configure(HardwareSerial* ports[], const uint8_t* node_ids,
                              uint8_t num_nodes, uint32_t baud) {
    m_num_nodes = (num_nodes <= 6) ? num_nodes : 6;
    m_baud = baud;

    for (uint8_t i = 0; i < m_num_nodes; i++) {
        m_ports[i] = ports[i];
        m_node_ids[i] = node_ids[i];
    }
}

bool MultiUartBus::begin() {
    for (uint8_t i = 0; i < m_num_nodes; i++) {
        if (m_ports[i] != nullptr) {
            m_ports[i]->begin(m_baud);
        }
    }
    return true;
}

HardwareSerial* MultiUartBus::getPortForNode(uint8_t node_id) {
    for (uint8_t i = 0; i < m_num_nodes; i++) {
        if (m_node_ids[i] == node_id) {
            return m_ports[i];
        }
    }
    return nullptr;
}

void MultiUartBus::sendRequest(uint8_t node_id, const uint8_t* data, size_t len) {
    HardwareSerial* port = getPortForNode(node_id);
    if (port != nullptr) {
        port->write(data, len);
        port->flush();  // Wait for TX to complete
    }
}

bool MultiUartBus::isReplyAvailable(uint8_t node_id) {
    HardwareSerial* port = getPortForNode(node_id);
    if (port != nullptr) {
        return port->available() > 0;
    }
    return false;
}

size_t MultiUartBus::readReply(uint8_t node_id, uint8_t* buffer, size_t max_len,
                                uint32_t timeout_us) {
    HardwareSerial* port = getPortForNode(node_id);
    if (port == nullptr) return 0;

    size_t idx = 0;
    uint32_t start = micros();

    while (idx < max_len) {
        // Check timeout
        if ((micros() - start) >= timeout_us) {
            break;
        }

        if (port->available()) {
            buffer[idx++] = port->read();
        }
    }

    return idx;
}

void MultiUartBus::flushRx(uint8_t node_id) {
    HardwareSerial* port = getPortForNode(node_id);
    if (port != nullptr) {
        while (port->available()) {
            port->read();
        }
    }
}

//=============================================================================
// UartHostStream Implementation
//=============================================================================

void UartHostStream::configure(HardwareSerial& port, uint32_t baud) {
    m_port = &port;
    m_baud = baud;
}

bool UartHostStream::begin() {
    if (m_port != nullptr) {
        m_port->begin(m_baud);
        return true;
    }
    return false;
}

void UartHostStream::sendFrame(const uint8_t* data, size_t len) {
    if (m_port != nullptr) {
        m_port->write(data, len);
    }
}

bool UartHostStream::available() {
    if (m_port != nullptr) {
        return m_port->available() > 0;
    }
    return false;
}

size_t UartHostStream::read(uint8_t* buffer, size_t max_len) {
    if (m_port == nullptr) return 0;

    size_t idx = 0;
    while (idx < max_len && m_port->available()) {
        buffer[idx++] = m_port->read();
    }
    return idx;
}
