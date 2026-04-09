/*
 * Master Configuration - Teensy 4.1
 *
 * Pin assignments and timing constants for encoder network master.
 *
 * Ref: Serial_master_node_architecture.md Section 9.1
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

//=============================================================================
// Node Configuration
//=============================================================================
constexpr uint8_t NUM_CONFIGURED_NODES = 7;     // Nodes 1-7
constexpr uint8_t NODE_IDS[] = {1, 2, 3, 4, 5, 6, 7};

//=============================================================================
// UART Configuration (Node Communication) - Teensy 4.1 Serial Ports
//=============================================================================
// Node 1: Serial1 (TX=1, RX=0)
#define NODE1_SERIAL    Serial1
constexpr uint8_t NODE1_TX_PIN = 1;
constexpr uint8_t NODE1_RX_PIN = 0;

// Node 2: Serial2 (TX=8, RX=7)
#define NODE2_SERIAL    Serial2
constexpr uint8_t NODE2_TX_PIN = 8;
constexpr uint8_t NODE2_RX_PIN = 7;

// Node 3: Serial3 (TX=14, RX=15)
#define NODE3_SERIAL    Serial3
constexpr uint8_t NODE3_TX_PIN = 14;
constexpr uint8_t NODE3_RX_PIN = 15;

// Node 4: Serial4 (TX=17, RX=16)
#define NODE4_SERIAL    Serial4
constexpr uint8_t NODE4_TX_PIN = 17;
constexpr uint8_t NODE4_RX_PIN = 16;

// Node 5: Serial5 (TX=20, RX=21)
#define NODE5_SERIAL    Serial5
constexpr uint8_t NODE5_TX_PIN = 20;
constexpr uint8_t NODE5_RX_PIN = 21;

// Node 6: Serial6 (TX=24, RX=25)
#define NODE6_SERIAL    Serial6
constexpr uint8_t NODE6_TX_PIN = 24;
constexpr uint8_t NODE6_RX_PIN = 25;

// Node 7: Serial7 (TX=29, RX=28)
#define NODE7_SERIAL    Serial7
constexpr uint8_t NODE7_TX_PIN = 29;
constexpr uint8_t NODE7_RX_PIN = 28;

// Common UART settings
constexpr uint32_t NODE_BAUD = 115200;

//=============================================================================
// Host Stream Configuration (PC Communication)
//=============================================================================
// Host: Serial8 (TX=35, RX=34) -> FTDI -> PC
#define HOST_SERIAL     Serial8
constexpr uint8_t HOST_TX_PIN = 35;
constexpr uint8_t HOST_RX_PIN = 34;
constexpr uint32_t HOST_BAUD = 115200;

//=============================================================================
// Timing Configuration (per architecture Section 4.1)
//=============================================================================
constexpr uint32_t SCHEDULER_TICK_US     = 1000;   // 1 ms tick
constexpr uint32_t BUS_POLL_PERIOD_MS    = 10;     // 100 Hz polling
constexpr uint32_t HOST_TX_PERIOD_MS     = 10;     // 100 Hz host stream
constexpr uint32_t DIAG_POLL_PERIOD_MS   = 1000;   // 1 Hz diagnostics
constexpr uint32_t STATUS_DISPLAY_PERIOD_MS = 1000; // 1 Hz dashboard refresh

//=============================================================================
// Fault Thresholds
//=============================================================================
constexpr uint16_t COMM_FAULT_THRESHOLD  = 100;    // consecutive misses -> fault
constexpr uint32_t REPLY_TIMEOUT_US      = 2000;   // 2 ms reply timeout

//=============================================================================
// Host Frame Sizes
//=============================================================================
// Host frame: START(1) + TIMESTAMP(4) + N_NODES(1) + NODE_DATA(10*N) + CRC(1)
// For 2 nodes: 1 + 4 + 1 + 20 + 1 = 27 bytes
constexpr uint8_t HOST_FRAME_HEADER_SIZE = 6;     // START + TIMESTAMP + N_NODES
constexpr uint8_t NODE_DATA_BLOCK_SIZE   = 10;    // ID + ANGLE(4) + VEL(4) + STATUS

inline uint8_t hostFrameSize(uint8_t num_nodes) {
    return HOST_FRAME_HEADER_SIZE + (num_nodes * NODE_DATA_BLOCK_SIZE) + 1;
}

//=============================================================================
// Debug Configuration
//=============================================================================
#define DEBUG_ENABLED       false
#define DEBUG_SERIAL        Serial      // USB Serial for debug output

#endif // CONFIG_H