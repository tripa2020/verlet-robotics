/*
 * Serial Interface - Hardware Abstraction Layer
 *
 * Abstract interfaces for node bus and host stream communication.
 * Provides INodeBus and IHostStream interfaces with concrete
 * implementations for point-to-point UART (V1).
 *
 * Ref: Serial_master_node_architecture.md Section 5.3 (serial_interface.h)
 */

#ifndef SERIAL_INTERFACE_H
#define SERIAL_INTERFACE_H

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

//=============================================================================
// INodeBus - Abstract interface for node communication
//=============================================================================
class INodeBus {
public:
    virtual ~INodeBus() = default;

    // Initialize the bus
    virtual bool begin() = 0;

    // Send request frame to a specific node
    virtual void sendRequest(uint8_t node_id, const uint8_t* data, size_t len) = 0;

    // Check if reply bytes are available for a node
    virtual bool isReplyAvailable(uint8_t node_id) = 0;

    // Read reply from a node (returns bytes read, 0 on timeout)
    virtual size_t readReply(uint8_t node_id, uint8_t* buffer, size_t max_len,
                             uint32_t timeout_us) = 0;

    // Flush any pending RX data for a node
    virtual void flushRx(uint8_t node_id) = 0;
};

//=============================================================================
// IHostStream - Abstract interface for host PC communication
//=============================================================================
class IHostStream {
public:
    virtual ~IHostStream() = default;

    // Initialize the host stream
    virtual bool begin() = 0;

    // Send frame to host
    virtual void sendFrame(const uint8_t* data, size_t len) = 0;

    // Check if host has sent data (for future commands)
    virtual bool available() = 0;

    // Read from host (for future commands)
    virtual size_t read(uint8_t* buffer, size_t max_len) = 0;
};

//=============================================================================
// MultiUartBus - Point-to-point UART implementation (V1)
//=============================================================================
class MultiUartBus : public INodeBus {
public:
    // Configure which serial port maps to which node
    // ports array indexed by node_id-1 (node 1 -> ports[0], etc.)
    void configure(HardwareSerial* ports[], const uint8_t* node_ids,
                   uint8_t num_nodes, uint32_t baud);

    bool   begin() override;
    void   sendRequest(uint8_t node_id, const uint8_t* data, size_t len) override;
    bool   isReplyAvailable(uint8_t node_id) override;
    size_t readReply(uint8_t node_id, uint8_t* buffer, size_t max_len,
                     uint32_t timeout_us) override;
    void   flushRx(uint8_t node_id) override;

private:
    HardwareSerial* getPortForNode(uint8_t node_id);

    HardwareSerial* m_ports[6] = {nullptr};
    uint8_t         m_node_ids[6] = {0};
    uint8_t         m_num_nodes = 0;
    uint32_t        m_baud = 115200;
};

//=============================================================================
// UartHostStream - UART to PC implementation (V1)
//=============================================================================
class UartHostStream : public IHostStream {
public:
    void configure(HardwareSerial& port, uint32_t baud);

    bool   begin() override;
    void   sendFrame(const uint8_t* data, size_t len) override;
    bool   available() override;
    size_t read(uint8_t* buffer, size_t max_len) override;

private:
    HardwareSerial* m_port = nullptr;
    uint32_t        m_baud = 115200;
};

#endif // SERIAL_INTERFACE_H
