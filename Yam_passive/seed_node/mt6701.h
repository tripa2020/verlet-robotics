/*
 * MT6701 SPI Driver - XIAO RP2040
 *
 * 14-bit magnetic encoder over SSI (3-wire SPI, read-only).
 * Frame: 24 bits = 14-bit angle + 4-bit status + 6-bit CRC.
 *
 * Ref: MT6701 datasheet, Serial_master_node_architecture.md
 */

#ifndef MT6701_H
#define MT6701_H

#include <stdint.h>

//=============================================================================
// MT6701 Field Status (from 24-bit frame bits 7-6)
//=============================================================================
constexpr uint8_t MT6701_FIELD_NORMAL = 0;
constexpr uint8_t MT6701_FIELD_STRONG = 1;
constexpr uint8_t MT6701_FIELD_WEAK   = 2;

//=============================================================================
// Read Result Structure
//=============================================================================
struct MT6701_Result {
    uint16_t raw_angle;     // 14-bit raw count (0-16383)
    float    angle_rad;     // converted to radians
    uint8_t  field_status;  // 0=normal, 1=strong, 2=weak
    bool     crc_valid;     // true if CRC6 matches
    bool     read_ok;       // true if SPI transfer succeeded
};

//=============================================================================
// API Functions
//=============================================================================

// Initialize SPI for MT6701
void mt6701_init();

// Read single sample (call from main loop, not ISR)
MT6701_Result mt6701_read();

// Verify CRC6 of 24-bit frame (returns true if valid)
bool mt6701_verifyCRC6(uint32_t frame);

#endif // MT6701_H