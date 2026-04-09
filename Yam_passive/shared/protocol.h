/*
 * Protocol Header - Shared between Master and Node
 *
 * Binary protocol definitions for MT6701 encoder network.
 * CRC8 polynomial 0x07, init 0x00.
 *
 * Ref: Serial_master_node_architecture.md Appendix A
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

//=============================================================================
// Frame Start Bytes
//=============================================================================
constexpr uint8_t FRAME_START_REQUEST     = 0xAA;
constexpr uint8_t FRAME_START_SAMPLE      = 0xBB;
constexpr uint8_t FRAME_START_DIAGNOSTIC  = 0xBC;
constexpr uint8_t FRAME_START_PING        = 0xBD;
constexpr uint8_t FRAME_START_HOST        = 0xCC;

//=============================================================================
// Command Codes
//=============================================================================
constexpr uint8_t CMD_GET_SAMPLE = 0x01;
constexpr uint8_t CMD_GET_DIAG   = 0x02;
constexpr uint8_t CMD_PING       = 0x03;
constexpr uint8_t CMD_ZERO       = 0x04;

//=============================================================================
// Frame Sizes
//=============================================================================
constexpr uint8_t FRAME_SIZE_REQUEST    = 4;
constexpr uint8_t FRAME_SIZE_SAMPLE     = 12;
constexpr uint8_t FRAME_SIZE_DIAGNOSTIC = 15;
constexpr uint8_t FRAME_SIZE_PING       = 4;

//=============================================================================
// Status Flags (single source of truth)
//=============================================================================
constexpr uint8_t STATUS_VALID        = 0x01;  // Bit 0: sample is valid
constexpr uint8_t STATUS_STALE        = 0x02;  // Bit 1: sample is stale
constexpr uint8_t STATUS_SENSOR_FAULT = 0x04;  // Bit 2: sensor fault threshold exceeded
constexpr uint8_t STATUS_FIELD_WEAK   = 0x08;  // Bit 3: MT6701 weak field
constexpr uint8_t STATUS_FIELD_STRONG = 0x10;  // Bit 4: MT6701 strong field
constexpr uint8_t STATUS_CRC_ERROR    = 0x20;  // Bit 5: recent SPI CRC error

// Status flag accessors (inline, zero overhead)
inline bool statusIsValid(uint8_t s)       { return s & STATUS_VALID; }
inline bool statusIsStale(uint8_t s)       { return s & STATUS_STALE; }
inline bool statusIsFaulted(uint8_t s)     { return s & STATUS_SENSOR_FAULT; }
inline bool statusIsFieldWeak(uint8_t s)   { return s & STATUS_FIELD_WEAK; }
inline bool statusIsFieldStrong(uint8_t s) { return s & STATUS_FIELD_STRONG; }
inline bool statusHasCrcError(uint8_t s)   { return s & STATUS_CRC_ERROR; }

// Status flag setters (for node-side use)
inline void statusSetValid(uint8_t& s)       { s |= STATUS_VALID; }
inline void statusSetStale(uint8_t& s)       { s |= STATUS_STALE; }
inline void statusSetFaulted(uint8_t& s)     { s |= STATUS_SENSOR_FAULT; }
inline void statusSetFieldWeak(uint8_t& s)   { s |= STATUS_FIELD_WEAK; }
inline void statusSetFieldStrong(uint8_t& s) { s |= STATUS_FIELD_STRONG; }
inline void statusSetCrcError(uint8_t& s)    { s |= STATUS_CRC_ERROR; }

inline void statusClearValid(uint8_t& s)     { s &= ~STATUS_VALID; }
inline void statusClearStale(uint8_t& s)     { s &= ~STATUS_STALE; }
inline void statusClearAll(uint8_t& s)       { s = 0; }

//=============================================================================
// Reset Cause Codes (v2 - detection deferred, always reports UNKNOWN for v1)
//=============================================================================
constexpr uint8_t RESET_POWER_ON   = 0x00;
constexpr uint8_t RESET_WATCHDOG   = 0x01;
constexpr uint8_t RESET_SOFT       = 0x02;
constexpr uint8_t RESET_UNKNOWN    = 0x03;  // default for v1

//=============================================================================
// CRC8 Function
//=============================================================================
uint8_t crc8(const uint8_t* data, size_t len);

#endif // PROTOCOL_H