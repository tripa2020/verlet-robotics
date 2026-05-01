/*
 * Master Configuration — Teensy 4.1
 *
 * CAN configuration, timing constants, and node list
 * for CAN encoder network master.
 *
 * Ref: DOCS/Architecture.md Section 2
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

//=============================================================================
// CAN Configuration
//=============================================================================
// Using CAN2 on Teensy 4.1 (pins 0/1)
// CAN1 is pins 22/23, also available if needed

constexpr uint32_t CAN_BAUD = 1000000;  // 1 Mbps

//=============================================================================
// Node Configuration
//=============================================================================
constexpr uint8_t NUM_NODES = 7;
constexpr uint8_t NODE_IDS[NUM_NODES] = {1, 2, 3, 4, 5, 6, 7};

// Minimum nodes required to enter RUNNING state
// Set to 0 for development (accept any number of nodes)
constexpr uint8_t MIN_NODES_REQUIRED = 0;

//=============================================================================
// Timing Configuration
//=============================================================================
constexpr uint32_t POLL_PERIOD_MS       = 10;       // 100 Hz poll rate
constexpr uint32_t REPLY_TIMEOUT_US     = 2000;     // 2 ms timeout per node
constexpr uint32_t DIAG_PERIOD_MS       = 1000;     // 1 Hz diagnostic poll
constexpr uint32_t HOST_STREAM_PERIOD_MS = 10;      // 100 Hz host stream
constexpr uint32_t INIT_RETRY_MS        = 1000;     // Retry INIT every 1 sec if nodes missing
constexpr uint32_t DISCOVERY_TIMEOUT_MS = 5000;     // Wait 5 sec for nodes at startup

//=============================================================================
// Fault Thresholds
//=============================================================================
constexpr uint16_t COMM_FAULT_THRESHOLD = 100;      // Consecutive misses → FAULTED

//=============================================================================
// Host Interface
//=============================================================================
// Using USB Serial for host communication
// Serial8 available if external FTDI needed (pins 34/35)

//=============================================================================
// Debug Configuration
//=============================================================================
#define DEBUG_ENABLED           true
#define DEBUG_PRINT_PERIOD_MS   1000    // Print stats every 1 sec

#endif // CONFIG_H
