/*
 * Node Configuration — XIAO RA4M1
 *
 * Pin assignments and timing constants for CAN encoder node.
 *
 * Ref: DOCS/Architecture.md Section 2
 */

#ifndef CONFIG_H
#define CONFIG_H

//=============================================================================
// Node Identity — CHANGE THIS FOR EACH NODE
//=============================================================================
#define NODE_ID     1      // 1-7, unique per node

//=============================================================================
// MT6701 SPI Configuration
//=============================================================================
#define SPI_CS_PIN      5       // D5
#define SPI_SCK_PIN     8       // D8
#define SPI_MISO_PIN    9       // D9
#define SPI_CLOCK_HZ    1000000 // 1 MHz
#define SPI_MODE        SPI_MODE1   // CPOL=0, CPHA=1

//=============================================================================
// Sampling Configuration
//=============================================================================
#define SAMPLE_PERIOD_US    2000    // 2 ms = 500 Hz (internal, when not SYNC-driven)
#define EMA_ALPHA           0.4f    // EMA filter coefficient

//=============================================================================
// Fault Thresholds
//=============================================================================
#define SENSOR_FAULT_THRESHOLD      500     // Consecutive SPI failures → FAULTED
#define SENSOR_DEGRADED_THRESHOLD   10      // Consecutive SPI failures → DEGRADED
#define SENSOR_RECOVERY_THRESHOLD   50      // Consecutive good reads → clear FAULTED

//=============================================================================
// CAN Configuration
//=============================================================================
// CAN pins are D17 (RX) and D18 (TX) — requires modified pins_arduino.h
// Baud rate set via Arduino_CAN library: CanBitRate::BR_1000k

//=============================================================================
// LED Configuration
//=============================================================================
#define LED_PIN     LED_BUILTIN     // Status LED

//=============================================================================
// Debug Configuration
//=============================================================================
#define DEBUG_ENABLED           true
#define DEBUG_SAMPLE_INTERVAL   500     // Print every N samples (500 = 1 Hz at 500 Hz)

//=============================================================================
// MT6701 Constants
//=============================================================================
#ifndef TWO_PI
#define TWO_PI 6.28318530718f
#endif

constexpr float COUNTS_PER_REV = 16384.0f;
#define RAD_PER_COUNT (TWO_PI / COUNTS_PER_REV)

#endif // CONFIG_H
