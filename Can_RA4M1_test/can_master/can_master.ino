/*
 * CAN Master — Teensy 4.1 CAN2
 *
 * Broadcasts POLL frame at 100Hz, waits for ACK from nodes.
 * Prints only on missed responses (silence = success).
 *
 * Hardware:
 *   Teensy 4.1 CAN2: CRX2=pin 0, CTX2=pin 1
 *   CAN Pal transceiver connected to CAN2 pins
 *   Termination: CAN Pal has onboard 120 ohm
 *
 * Protocol:
 *   POLL: ID=0x100, DLC=1, payload=[seq_num]
 *   ACK:  ID=0x200+node_id, DLC=2, payload=[seq_num, status]
 */

#include <FlexCAN_T4.h>
#include "config.h"

FlexCAN_T4<CAN2, RX_SIZE_256, TX_SIZE_16> can2;

// State
static uint8_t  g_seq = 0;
static uint32_t g_last_poll_ms = 0;
static uint32_t g_last_print_ms = 0;

// Stats
static uint32_t g_poll_count = 0;
static uint32_t g_ack_count[NUM_NODES] = {0};
static uint32_t g_last_poll_count = 0;
static uint32_t g_last_ack_count = 0;

// Heartbeat RX tracking (tests RA4M1 TX path)
constexpr uint32_t CAN_ID_HEARTBEAT_BASE = 0x300;
static uint32_t g_heartbeat_rx_count = 0;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);

    Serial.println("=== CAN Master (Teensy CAN2) ===");
    Serial.printf("Baud: %lu, Poll: %lu Hz, Nodes: %d\n",
                  CAN_BAUD, 1000 / POLL_PERIOD_MS, NUM_NODES);

    can2.begin();
    can2.setBaudRate(CAN_BAUD);

    Serial.println("Ready.\n");
}

void loop() {
    uint32_t now = millis();

    // Poll task (100 Hz)
    if (now - g_last_poll_ms >= POLL_PERIOD_MS) {
        g_last_poll_ms = now;
        g_poll_count++;
        g_seq++;

        // Send POLL
        CAN_message_t poll;
        poll.id = CAN_ID_POLL;
        poll.len = 1;
        poll.buf[0] = g_seq;
        can2.write(poll);

        // Wait for responses
        bool received[NUM_NODES] = {false};
        uint32_t deadline = micros() + RESPONSE_TIMEOUT_US;

        while (micros() < deadline) {
            CAN_message_t msg;
            if (can2.read(msg)) {
                // Check for ACK
                for (uint8_t i = 0; i < NUM_NODES; i++) {
                    if (msg.id == CAN_ID_ACK_BASE + NODE_IDS[i] &&
                        msg.buf[0] == g_seq) {
                        received[i] = true;
                        g_ack_count[i]++;
                    }
                }
                // Check for heartbeat (0x301 for node 1)
                if (msg.id >= CAN_ID_HEARTBEAT_BASE && msg.id < CAN_ID_HEARTBEAT_BASE + 8) {
                    g_heartbeat_rx_count++;
                    Serial.printf("[HB_RX id=0x%X seq=%u]\n", msg.id,
                        msg.buf[1] | (msg.buf[2] << 8));
                }
            }
        }

        // Track misses (don't print here)
        for (uint8_t i = 0; i < NUM_NODES; i++) {
            if (!received[i]) {
                // Miss tracked via g_ack_count not incrementing
            }
        }
    }

    // Stats print (1 Hz)
    if (now - g_last_print_ms >= PRINT_PERIOD_MS) {
        g_last_print_ms = now;
        uint32_t period_polls = g_poll_count - g_last_poll_count;
        uint32_t period_acks = g_ack_count[0] - g_last_ack_count;
        uint32_t period_miss = period_polls - period_acks;

        Serial.printf("N1: %lu/%lu (%lu miss) | HB_RX: %lu\n",
            period_acks, period_polls, period_miss, g_heartbeat_rx_count);

        g_last_poll_count = g_poll_count;
        g_last_ack_count = g_ack_count[0];
    }
}
