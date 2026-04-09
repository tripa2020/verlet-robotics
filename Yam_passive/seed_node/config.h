/*
 * Node Configuration - XIAO RP2040
 *
 * Pin assignments and timing constants for encoder node.
 *
 * Ref: Serial_master_node_architecture.md Section 9.2
 */

#ifndef CONFIG_H
#define CONFIG_H

//=============================================================================
// Node Identity
//=============================================================================
#define NODE_ID     6      // Change per node (1-6)

//=============================================================================
// UART Configuration (to Master)
//=============================================================================
#define UART_TX_PIN     0       // D6 = GPIO0 (UART0 TX)
#define UART_RX_PIN     1       // D7 = GPIO1 (UART0 RX)
#define UART_BAUD       115200

//=============================================================================
// MT6701 SPI Configuration
//=============================================================================
#define SPI_CS_PIN      6       // D5 = GPIO7
#define SPI_SCK_PIN     2       // D8 = GPIO2
#define SPI_MISO_PIN    4       // D9 = GPIO4
#define SPI_CLOCK_HZ    1000000 // 1 MHz
#define SPI_MODE        SPI_MODE1   // CPOL=0, CPHA=1

//=============================================================================
// Sampling Configuration
//=============================================================================
#define SAMPLE_PERIOD_US    2000    // 2 ms = 500 Hz
#define EMA_ALPHA           0.4f    // EMA filter coefficient

//=============================================================================
// Fault Thresholds
//=============================================================================
#define SENSOR_FAULT_THRESHOLD  500     // consecutive SPI failures

//=============================================================================
// LED Configuration
//=============================================================================
#define LED_PIN     25      // User LED (built-in)

//=============================================================================
// MT6701 Constants (TWO_PI provided by Arduino)
//=============================================================================
constexpr float COUNTS_PER_REV = 16384.0f;
#define RAD_PER_COUNT (TWO_PI / COUNTS_PER_REV)

#endif // CONFIG_H