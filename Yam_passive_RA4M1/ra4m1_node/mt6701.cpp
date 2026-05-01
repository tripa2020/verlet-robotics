/*
 * MT6701 SPI Driver Implementation — XIAO RA4M1
 *
 * Uses hardware SPI on RA4M1 at 1 MHz.
 * Includes CRC6 verification and stuck-line detection.
 *
 * Adapted from Yam_passive seed_node for RA4M1.
 *
 * Ref: MT6701 datasheet, DOCS/Architecture.md
 */

#include "mt6701.h"
#include "config.h"
#include <Arduino.h>
#include <SPI.h>

//=============================================================================
// CRC6 Calculation - MT6701 uses polynomial 0x03 (x^6 + x + 1)
//=============================================================================
static uint8_t calcCRC6(uint32_t data18) {
    // CRC6 over 18 bits of data (angle[13:0] + status[3:0])
    // Polynomial: x^6 + x + 1 = 0x43 (with implicit x^6)
    uint8_t crc = 0x00;

    for (int i = 17; i >= 0; i--) {
        uint8_t bit = (data18 >> i) & 0x01;
        uint8_t xor_bit = (crc >> 5) & 0x01;

        crc = (crc << 1) & 0x3F;  // Shift and mask to 6 bits

        if (bit ^ xor_bit) {
            crc ^= 0x03;  // XOR with polynomial (x + 1)
        }
    }

    return crc & 0x3F;
}

//=============================================================================
// Initialization
//=============================================================================
void mt6701_init() {
    // Configure pins
    pinMode(SPI_CS_PIN, OUTPUT);
    digitalWrite(SPI_CS_PIN, HIGH);

    // Initialize SPI
    // Note: RA4M1 SPI uses default pins D8 (SCK), D9 (MISO), D10 (MOSI)
    // We only use SCK and MISO for MT6701 (read-only SSI)
    SPI.begin();
}

//=============================================================================
// Read Sample with CRC6 verification and sanity checks
//=============================================================================
MT6701_Result mt6701_read() {
    MT6701_Result result = {0, 0.0f, 0, false, false};
    uint8_t data[3];

    // SPI read - MT6701 requires MODE1 (CPOL=0, CPHA=1)
    SPI.beginTransaction(SPISettings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE));
    digitalWrite(SPI_CS_PIN, LOW);
    delayMicroseconds(1);

    data[0] = SPI.transfer(0xFF);
    data[1] = SPI.transfer(0xFF);
    data[2] = SPI.transfer(0xFF);

    digitalWrite(SPI_CS_PIN, HIGH);
    SPI.endTransaction();

    // Sanity check: detect stuck MISO line (all 0s or all 1s)
    if ((data[0] == 0xFF && data[1] == 0xFF && data[2] == 0xFF) ||
        (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00)) {
        // MISO stuck - likely disconnected
        result.read_ok = false;
        result.crc_valid = false;
        return result;
    }

    // Parse 24-bit frame:
    //   Bits 23-10: 14-bit angle
    //   Bits 9-6:   4-bit status (field[1:0], push_status, push_flag)
    //   Bits 5-0:   6-bit CRC
    uint32_t frame = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];

    // Extract fields
    result.raw_angle = (frame >> 10) & 0x3FFF;           // Bits 23-10
    uint8_t status_4bit = (frame >> 6) & 0x0F;           // Bits 9-6
    uint8_t crc_received = frame & 0x3F;                 // Bits 5-0

    // Field status is lower 2 bits of status
    result.field_status = status_4bit & 0x03;

    // CRC6 verification over upper 18 bits
    uint32_t data18 = frame >> 6;
    uint8_t crc_calc = calcCRC6(data18);
    result.crc_valid = (crc_calc == crc_received);

    // Convert to radians
    result.angle_rad = (float)result.raw_angle * RAD_PER_COUNT;

    // Mark read as successful (SPI transfer completed)
    result.read_ok = true;

    return result;
}

//=============================================================================
// CRC6 Verification (public API)
//=============================================================================
bool mt6701_verifyCRC6(uint32_t frame) {
    uint32_t data18 = frame >> 6;
    uint8_t crc_received = frame & 0x3F;
    uint8_t crc_calc = calcCRC6(data18);
    return crc_calc == crc_received;
}
