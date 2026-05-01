/*
 * CAN Protocol Definitions — Yam_passive_RA4M1
 *
 * Frame IDs, sizes, encoding macros, and status flags
 * for the CAN encoder network.
 *
 * Ref: DOCS/Architecture.md Section 3
 */

#ifndef CAN_PROTOCOL_H
#define CAN_PROTOCOL_H

#include <stdint.h>

//=============================================================================
// CAN IDs (lower ID = higher priority)
//=============================================================================

// Master → All
constexpr uint16_t CAN_ID_SYNC        = 0x000;  // Trigger synchronized sampling
constexpr uint16_t CAN_ID_INIT        = 0x010;  // Enable nodes to respond

// Node → Master (unsolicited)
constexpr uint16_t CAN_ID_FAULT_BASE  = 0x0F0;  // + node_id (0x0F1-0x0F7)

// Master → All
constexpr uint16_t CAN_ID_POLL_SAMPLE = 0x100;  // Request sample from all

// Master → Node
constexpr uint16_t CAN_ID_POLL_DIAG   = 0x1F0;  // + node_id (0x1F1-0x1F7)

// Node → Master
constexpr uint16_t CAN_ID_SAMPLE_BASE = 0x200;  // + node_id (0x201-0x207)
constexpr uint16_t CAN_ID_DIAG_BASE   = 0x2F0;  // + node_id (0x2F1-0x2F7)

//=============================================================================
// Frame Sizes (DLC)
//=============================================================================
constexpr uint8_t FRAME_SIZE_SYNC         = 0;
constexpr uint8_t FRAME_SIZE_INIT         = 1;
constexpr uint8_t FRAME_SIZE_POLL_SAMPLE  = 0;
constexpr uint8_t FRAME_SIZE_POLL_DIAG    = 0;
constexpr uint8_t FRAME_SIZE_SAMPLE_REPLY = 8;
constexpr uint8_t FRAME_SIZE_DIAG_REPLY   = 8;
constexpr uint8_t FRAME_SIZE_FAULT_EVENT  = 8;

//=============================================================================
// INIT Frame Flags
//=============================================================================
constexpr uint8_t INIT_FLAG_ENABLE = 0x01;  // Bit 0: enable responses

//=============================================================================
// Status Flags (shared between node and master)
//=============================================================================
constexpr uint8_t STATUS_VALID        = 0x01;  // Bit 0: sample is valid
constexpr uint8_t STATUS_STALE        = 0x02;  // Bit 1: data is stale (frozen)
constexpr uint8_t STATUS_SENSOR_FAULT = 0x04;  // Bit 2: sensor fault threshold
constexpr uint8_t STATUS_FIELD_WEAK   = 0x08;  // Bit 3: MT6701 weak field
constexpr uint8_t STATUS_FIELD_STRONG = 0x10;  // Bit 4: MT6701 strong field
constexpr uint8_t STATUS_CRC_ERROR    = 0x20;  // Bit 5: recent SPI CRC error
constexpr uint8_t STATUS_DEGRADED     = 0x40;  // Bit 6: node degraded
constexpr uint8_t STATUS_RECOVERING   = 0x80;  // Bit 7: node recovering

// Status flag accessors
inline bool statusIsValid(uint8_t s)       { return s & STATUS_VALID; }
inline bool statusIsStale(uint8_t s)       { return s & STATUS_STALE; }
inline bool statusIsFaulted(uint8_t s)     { return s & STATUS_SENSOR_FAULT; }
inline bool statusIsFieldWeak(uint8_t s)   { return s & STATUS_FIELD_WEAK; }
inline bool statusIsFieldStrong(uint8_t s) { return s & STATUS_FIELD_STRONG; }
inline bool statusHasCrcError(uint8_t s)   { return s & STATUS_CRC_ERROR; }
inline bool statusIsDegraded(uint8_t s)    { return s & STATUS_DEGRADED; }
inline bool statusIsRecovering(uint8_t s)  { return s & STATUS_RECOVERING; }

// Status flag setters
inline void statusSetValid(uint8_t& s)       { s |= STATUS_VALID; }
inline void statusSetStale(uint8_t& s)       { s |= STATUS_STALE; }
inline void statusSetFaulted(uint8_t& s)     { s |= STATUS_SENSOR_FAULT; }
inline void statusSetFieldWeak(uint8_t& s)   { s |= STATUS_FIELD_WEAK; }
inline void statusSetFieldStrong(uint8_t& s) { s |= STATUS_FIELD_STRONG; }
inline void statusSetCrcError(uint8_t& s)    { s |= STATUS_CRC_ERROR; }
inline void statusSetDegraded(uint8_t& s)    { s |= STATUS_DEGRADED; }
inline void statusSetRecovering(uint8_t& s)  { s |= STATUS_RECOVERING; }

inline void statusClearValid(uint8_t& s)     { s &= ~STATUS_VALID; }
inline void statusClearStale(uint8_t& s)     { s &= ~STATUS_STALE; }
inline void statusClearCrcError(uint8_t& s)  { s &= ~STATUS_CRC_ERROR; }
inline void statusClearAll(uint8_t& s)       { s = 0; }

//=============================================================================
// Fault States
//=============================================================================
constexpr uint8_t FAULT_STATE_INIT       = 0;
constexpr uint8_t FAULT_STATE_HEALTHY    = 1;
constexpr uint8_t FAULT_STATE_DEGRADED   = 2;
constexpr uint8_t FAULT_STATE_FAULTED    = 3;
constexpr uint8_t FAULT_STATE_RECOVERING = 4;

//=============================================================================
// Encoding Constants
//=============================================================================

// Angle: 16-bit unsigned → 0 to 2π radians
// Resolution: 2π / 65536 ≈ 0.000096 rad ≈ 0.0055°
constexpr float ANGLE_SCALE = 65536.0f / 6.28318530718f;  // rad → raw
constexpr float ANGLE_SCALE_INV = 6.28318530718f / 65536.0f;  // raw → rad

// Velocity: 16-bit signed → ±100 rad/s
// Resolution: 100 / 32767 ≈ 0.003 rad/s
constexpr float VELOCITY_MAX = 100.0f;
constexpr float VELOCITY_SCALE = 32767.0f / VELOCITY_MAX;  // rad/s → raw
constexpr float VELOCITY_SCALE_INV = VELOCITY_MAX / 32767.0f;  // raw → rad/s

// Acceleration: 16-bit signed → ±1000 rad/s² (future)
constexpr float ACCEL_MAX = 1000.0f;
constexpr float ACCEL_SCALE = 32767.0f / ACCEL_MAX;
constexpr float ACCEL_SCALE_INV = ACCEL_MAX / 32767.0f;

//=============================================================================
// Encoding Functions
//=============================================================================

// Angle: radians → uint16
inline uint16_t encodeAngle(float rad) {
    // Normalize to [0, 2π)
    while (rad < 0.0f) rad += 6.28318530718f;
    while (rad >= 6.28318530718f) rad -= 6.28318530718f;
    return (uint16_t)(rad * ANGLE_SCALE);
}

// Angle: uint16 → radians
inline float decodeAngle(uint16_t raw) {
    return (float)raw * ANGLE_SCALE_INV;
}

// Velocity: rad/s → int16 (clamped to ±100)
inline int16_t encodeVelocity(float rad_s) {
    if (rad_s > VELOCITY_MAX) rad_s = VELOCITY_MAX;
    if (rad_s < -VELOCITY_MAX) rad_s = -VELOCITY_MAX;
    return (int16_t)(rad_s * VELOCITY_SCALE);
}

// Velocity: int16 → rad/s
inline float decodeVelocity(int16_t raw) {
    return (float)raw * VELOCITY_SCALE_INV;
}

// Acceleration: rad/s² → int16 (clamped to ±1000)
inline int16_t encodeAccel(float rad_s2) {
    if (rad_s2 > ACCEL_MAX) rad_s2 = ACCEL_MAX;
    if (rad_s2 < -ACCEL_MAX) rad_s2 = -ACCEL_MAX;
    return (int16_t)(rad_s2 * ACCEL_SCALE);
}

// Acceleration: int16 → rad/s²
inline float decodeAccel(int16_t raw) {
    return (float)raw * ACCEL_SCALE_INV;
}

//=============================================================================
// Host Frame Markers (USB Serial to PC)
//=============================================================================
constexpr uint8_t HOST_FRAME_START = 0xCC;
constexpr uint8_t HOST_ALERT_START = 0xCD;

//=============================================================================
// CRC8 (polynomial 0x07, init 0x00) — for host frames
//=============================================================================
inline uint8_t crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

#endif // CAN_PROTOCOL_H
