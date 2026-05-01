#pragma once

// CAN Configuration
constexpr uint32_t CAN_BAUD = 1000000;  // 1 Mbps

// Frame IDs
constexpr uint32_t CAN_ID_POLL     = 0x100;
constexpr uint32_t CAN_ID_ACK_BASE = 0x200;  // Node N responds with 0x200 + N

// Timing
constexpr uint32_t POLL_PERIOD_MS      = 10;    // 100 Hz
constexpr uint32_t RESPONSE_TIMEOUT_US = 1000;  // 1 ms
constexpr uint32_t PRINT_PERIOD_MS     = 1000;  // Stats every 1 second

// Nodes
constexpr uint8_t NUM_NODES = 1;
constexpr uint8_t NODE_IDS[NUM_NODES] = {1};
