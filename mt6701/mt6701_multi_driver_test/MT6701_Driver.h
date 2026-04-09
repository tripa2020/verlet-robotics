/*
 * MT6701_Driver - Multi-encoder SSI driver for Teensy 4.1
 *
 * Features:
 *   - Up to 5 MT6701 encoders on SPI1 bus
 *   - CRC6 validation with configurable fault threshold
 *   - EMA low-pass filtering
 *   - Velocity estimation from filtered position
 *   - Health state machine (OK/DEGRADED/FAULTED)
 *
 * Hardware:
 *   - SPI1: SCK=27, MISO=1
 *   - CS pins: configurable per encoder
 *
 * Reference: MT6701 datasheet, SmartKnob CRC6 implementation
 */

#ifndef MT6701_DRIVER_H
#define MT6701_DRIVER_H

#include <Arduino.h>
#include <SPI.h>

class MT6701_Driver {
public:
    // Constants
    static constexpr uint8_t MAX_ENCODERS = 5;
    static constexpr float DEG_PER_RAD = 180.0f / 3.14159265f;
    static constexpr float RAD_PER_DEG = 3.14159265f / 180.0f;
    static constexpr float TWO_PI_F = 6.28318530f;

    // Health states
    enum class Health : uint8_t {
        OK,       // Normal operation
        DEGRADED, // Warnings present (weak field, occasional CRC errors)
        FAULTED   // Critical failure, position invalid
    };

    // Field status from MT6701
    enum class FieldStatus : uint8_t {
        NORMAL = 0,
        STRONG = 1,
        WEAK   = 2,
        ERROR  = 3
    };

    // Configuration
    struct Config {
        uint8_t  cs_pins[MAX_ENCODERS]; // CS pin for each encoder
        uint8_t  num_encoders;          // 1 to MAX_ENCODERS
        uint32_t spi_clock_hz;          // Default 4000000
        float    filter_alpha;          // EMA alpha for position (0.0-1.0)
        uint8_t  crc_fail_threshold;    // Consecutive fails before fault
    };

    MT6701_Driver();

    // Initialization
    bool begin(const Config& config);

    // Reading - updates internal state and returns health
    Health read(uint8_t encoder_id);
    void   readAll();

    // Position and velocity (radians, rad/sec)
    float getAngle(uint8_t encoder_id) const;
    float getVelocity(uint8_t encoder_id) const;

    // Health
    Health getHealth(uint8_t encoder_id) const;
    bool   isFaulted(uint8_t encoder_id) const;
    bool   anyFaulted() const;
    void   reset(uint8_t encoder_id);

    // Diagnostics
    void printDiagnostics(Stream& out, bool verbose = false) const;

private:
    // Internal diagnostics (hidden from API)
    struct DiagnosticCounters {
        uint32_t crc_fail_total;
        uint32_t crc_fail_consecutive;
        uint32_t field_weak_count;
        uint32_t field_strong_count;
        uint32_t track_loss_count;
        uint32_t total_reads;
        uint32_t last_good_us;
    };

    // Per-encoder state
    struct EncoderState {
        float    angle;           // Filtered, radians
        float    prev_angle;      // Previous filtered angle
        float    velocity;        // rad/sec
        uint32_t last_read_us;
        Health   health;
        FieldStatus field_status;
        DiagnosticCounters diag;
    };

    EncoderState encoders_[MAX_ENCODERS];
    Config config_;
    bool initialized_;

    // CRC6 lookup table (64 entries)
    static const uint8_t crc6_table_[64];

    // CRC6 calculation over 18 bits (angle + status)
    uint8_t calculateCRC6(uint32_t data) const;

    // Read raw 24-bit frame from encoder
    uint32_t readRawFrame(uint8_t encoder_id);

    // Parse frame and update encoder state
    bool processFrame(uint32_t frame, uint8_t encoder_id);

    // Update health based on current diagnostics
    void updateHealth(uint8_t encoder_id);
};

#endif // MT6701_DRIVER_H
