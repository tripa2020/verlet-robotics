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
// Node Discovery State (M7)
//=============================================================================
static uint8_t g_active_nodes = 0;      // Bitmask of discovered nodes (bit 0 = node 1)
static uint8_t g_num_active = 0;        // Count of active nodes

//=============================================================================
// Node Sample Storage (M5)
//=============================================================================
struct NodeSample {
    float angle_rad;
    float velocity_rad_s;
    uint8_t status;
    bool received;        // Set each poll cycle
    bool active;          // Node responded to discovery (M7)
    uint32_t rx_count;    // Lifetime RX count
    uint32_t miss_count;  // Lifetime miss count
};
static NodeSample g_nodes[NUM_NODES];

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
// Transmit INIT Frame (ID 0x010, DLC 1) — M7
//=============================================================================
static bool txInit() {
    CAN_message_t msg;
    msg.id = CAN_ID_INIT;
    msg.len = FRAME_SIZE_INIT;
    msg.flags.extended = 0;
    msg.buf[0] = INIT_FLAG_ENABLE;

    return can2.write(msg);
}

//=============================================================================
// Node Discovery — M7
// Sends INIT, waits for SAMPLE_REPLY from any node
//=============================================================================
static void discoverNodes() {
    Serial.println("Discovering nodes...");

    // Clear active flags
    g_active_nodes = 0;
    g_num_active = 0;
    for (uint8_t i = 0; i < NUM_NODES; i++) {
        g_nodes[i].active = false;
    }

    // Send multiple INIT frames to ensure reception
    for (int attempt = 0; attempt < 3; attempt++) {
        txInit();
        delay(50);  // Give nodes time to respond

        // Collect responses
        uint32_t deadline = millis() + 100;
        while (millis() < deadline) {
            CAN_message_t msg;
            if (can2.read(msg)) {
                // Check for SAMPLE_REPLY (nodes respond with their sample frame)
                if (msg.id >= CAN_ID_SAMPLE_BASE + 1 && msg.id <= CAN_ID_SAMPLE_BASE + NUM_NODES) {
                    uint8_t node_id = msg.id - CAN_ID_SAMPLE_BASE;  // 1-7
                    uint8_t node_idx = node_id - 1;  // 0-indexed

                    if (node_idx < NUM_NODES && !g_nodes[node_idx].active) {
                        g_nodes[node_idx].active = true;
                        g_active_nodes |= (1 << node_idx);
                        g_num_active++;
                        Serial.printf("  Found node %d (ID 0x%03X)\n", node_id, msg.id);
                    }
                }
            }
        }
    }

    Serial.printf("Discovery complete: %d node(s) found\n", g_num_active);
    if (g_num_active == 0) {
        Serial.println("WARNING: No nodes detected!");
    }
    Serial.println();
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

    // M7: Discover connected nodes
    discoverNodes();

    Serial.println("Starting poll cycle...");
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

        // M5: RX SAMPLE_REPLY from nodes
        // Clear received flags for this cycle
        for (uint8_t i = 0; i < NUM_NODES; i++) {
            g_nodes[i].received = false;
        }

        // Wait for responses with timeout
        uint32_t deadline = micros() + REPLY_TIMEOUT_US;
        while (micros() < deadline) {
            CAN_message_t msg;
            if (can2.read(msg)) {
                // Check if this is a SAMPLE_REPLY (0x201-0x207)
                if (msg.id >= CAN_ID_SAMPLE_BASE + 1 && msg.id <= CAN_ID_SAMPLE_BASE + NUM_NODES) {
                    uint8_t node_idx = msg.id - CAN_ID_SAMPLE_BASE - 1;  // 0-indexed

                    if (node_idx < NUM_NODES && msg.len >= 7) {
                        // Decode payload
                        uint16_t angle_raw = msg.buf[0] | (msg.buf[1] << 8);
                        int16_t vel_raw = msg.buf[2] | (msg.buf[3] << 8);
                        uint8_t status = msg.buf[6];

                        g_nodes[node_idx].angle_rad = decodeAngle(angle_raw);
                        g_nodes[node_idx].velocity_rad_s = decodeVelocity(vel_raw);
                        g_nodes[node_idx].status = status;
                        g_nodes[node_idx].received = true;
                        g_nodes[node_idx].rx_count++;
                    }
                }
            }
        }

        // Track misses (only for active nodes)
        for (uint8_t i = 0; i < NUM_NODES; i++) {
            if (g_nodes[i].active && !g_nodes[i].received) {
                g_nodes[i].miss_count++;
            }
        }
    }

    // Debug output at 1 Hz
    #if DEBUG_ENABLED
    static uint32_t last_debug = 0;
    static uint32_t last_rx_total[NUM_NODES] = {0};

    if (now_ms - last_debug >= DEBUG_PRINT_PERIOD_MS) {
        last_debug = now_ms;

        // Header line
        Serial.printf("POLL #%lu | Nodes=%d | TX_ERR=%lu\n", poll_count, g_num_active, tx_errors);

        // Per-node stats (only active nodes)
        for (uint8_t i = 0; i < NUM_NODES; i++) {
            if (!g_nodes[i].active) continue;

            uint32_t period_rx = g_nodes[i].rx_count - last_rx_total[i];
            last_rx_total[i] = g_nodes[i].rx_count;

            Serial.printf("  N%d: %lu/100 ang=%.1f° vel=%.2f st=0x%02X\n",
                NODE_IDS[i],
                period_rx,
                g_nodes[i].angle_rad * 180.0f / 3.14159f,
                g_nodes[i].velocity_rad_s,
                g_nodes[i].status);
        }
    }
    #endif
}
