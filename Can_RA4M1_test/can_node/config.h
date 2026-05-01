#pragma once

// Node Identity — CHANGE THIS FOR EACH NODE
constexpr uint8_t NODE_ID = 1;  // Node 1 or 2

// Frame IDs (must match master)
constexpr uint32_t CAN_ID_POLL     = 0x100;
constexpr uint32_t CAN_ID_ACK_BASE = 0x200;

// Note: Arduino_CAN uses CanBitRate::BR_1000k for 1 Mbps
// Pins D17/D18 are native CAN (CRX0/CTX0) — handled by library
