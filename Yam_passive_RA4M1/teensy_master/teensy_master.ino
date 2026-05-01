/*
 * Teensy 4.1 — CAN Encoder Network Master
 *
 * Polls XIAO RA4M1 encoder nodes via CAN bus,
 * aggregates data, and streams to host PC.
 *
 * Board: Teensy 4.1
 * Select in Arduino IDE: Tools > Board > Teensy 4.1
 *
 * Wiring:
 *   CAN2: TX=pin 1, RX=pin 0 → TJA1051 transceiver
 *   Host: USB Serial
 *
 * Ref: DOCS/Architecture.md
 */

#include <Arduino.h>
#include <FlexCAN_T4.h>
#include "config.h"
#include <can_protocol.h>

//=============================================================================
// CAN Bus Instance
//=============================================================================
FlexCAN_T4<CAN2, RX_SIZE_256, TX_SIZE_16> can2;

//=============================================================================
// Poll Cycle State
//=============================================================================
static uint32_t last_poll_ms = 0;
static uint32_t poll_count = 0;
static uint32_t sync_tx_count = 0;
static uint32_t poll_tx_count = 0;
static uint32_t tx_errors = 0;

//=============================================================================
// Transmit SYNC Frame (ID 0x000, DLC 0)
//=============================================================================
static bool txSync() {
    CAN_message_t msg;
    msg.id = CAN_ID_SYNC;
    msg.len = FRAME_SIZE_SYNC;
    msg.flags.extended = 0;

    if (can2.write(msg)) {
        sync_tx_count++;
        return true;
    }
    tx_errors++;
    return false;
}

//=============================================================================
// Transmit POLL_SAMPLE Frame (ID 0x100, DLC 0)
//=============================================================================
static bool txPollSample() {
    CAN_message_t msg;
    msg.id = CAN_ID_POLL_SAMPLE;
    msg.len = FRAME_SIZE_POLL_SAMPLE;
    msg.flags.extended = 0;

    if (can2.write(msg)) {
        poll_tx_count++;
        return true;
    }
    tx_errors++;
    return false;
}

//=============================================================================
// Setup
//=============================================================================
void setup() {
    // USB serial for debug and host communication
    Serial.begin(115200);
    while (!Serial && millis() < 3000);

    // Initialize CAN2
    can2.begin();
    can2.setBaudRate(CAN_BAUD);

    // Print startup info
    Serial.println();
    Serial.println("=== Teensy 4.1 CAN Encoder Master ===");
    Serial.print("CAN baud: ");
    Serial.print(CAN_BAUD / 1000);
    Serial.println(" kbps");
    Serial.print("Expected nodes: ");
    Serial.println(NUM_NODES);
    Serial.print("Poll rate: ");
    Serial.print(1000 / POLL_PERIOD_MS);
    Serial.println(" Hz");
    Serial.println("CAN2 initialized on pins 0 (RX), 1 (TX)");
    Serial.println();
    Serial.println("Setup complete. Starting poll cycle...");
    Serial.println("======================================");
    Serial.println();

    // Initialize timing
    last_poll_ms = millis();
}

//=============================================================================
// Loop
//=============================================================================
void loop() {
    uint32_t now_ms = millis();

    // 100 Hz poll cycle
    if (now_ms - last_poll_ms >= POLL_PERIOD_MS) {
        last_poll_ms += POLL_PERIOD_MS;

        // Handle timing slip
        if (now_ms - last_poll_ms > POLL_PERIOD_MS * 2) {
            last_poll_ms = now_ms;
        }

        // TX SYNC frame (triggers node sampling)
        txSync();

        // Small delay for nodes to sample (~100 µs is enough, but
        // delayMicroseconds blocks - for M2 we just send immediately)
        // In production, SYNC and POLL could be combined or timed better

        // TX POLL_SAMPLE frame (requests replies)
        txPollSample();

        poll_count++;
    }

    // Debug output at 1 Hz
    #if DEBUG_ENABLED
    static uint32_t last_debug = 0;
    if (now_ms - last_debug >= DEBUG_PRINT_PERIOD_MS) {
        last_debug = now_ms;
        Serial.print("POLL #");
        Serial.print(poll_count);
        Serial.print(" | SYNC_TX=");
        Serial.print(sync_tx_count);
        Serial.print(" | POLL_TX=");
        Serial.print(poll_tx_count);
        Serial.print(" | TX_ERR=");
        Serial.println(tx_errors);
    }
    #endif

    // TODO (Milestone 5): CAN RX for SAMPLE_REPLY
    // TODO (Milestone 5): Host stream TX
    // TODO (Milestone 7): INIT command and startup sequence
}
