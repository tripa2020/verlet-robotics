/*
 * CAN Master Loopback Test — Teensy 4.1 CAN2
 *
 * Tests if FlexCAN_T4 is working by sending and receiving
 * in loopback mode (no external wiring needed).
 */

#include <FlexCAN_T4.h>

FlexCAN_T4<CAN2, RX_SIZE_256, TX_SIZE_16> can2;

static uint32_t g_tx_count = 0;
static uint32_t g_rx_count = 0;
static uint32_t g_last_print = 0;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);

    Serial.println("=== CAN2 Loopback Test ===");

    can2.begin();
    can2.setBaudRate(1000000);

    // Enable loopback mode - TX goes directly to RX internally
    can2.enableLoopBack();

    Serial.println("Loopback enabled. Sending test frames...");
}

void loop() {
    // Send a test frame every 100ms
    static uint32_t last_tx = 0;
    if (millis() - last_tx >= 100) {
        last_tx = millis();

        CAN_message_t msg;
        msg.id = 0x100;
        msg.len = 1;
        msg.buf[0] = g_tx_count & 0xFF;

        if (can2.write(msg)) {
            g_tx_count++;
        }
    }

    // Check for received frames
    CAN_message_t rx;
    if (can2.read(rx)) {
        g_rx_count++;
    }

    // Print stats every second
    if (millis() - g_last_print >= 1000) {
        g_last_print = millis();
        Serial.printf("TX: %lu  RX: %lu  %s\n",
            g_tx_count, g_rx_count,
            (g_rx_count == g_tx_count) ? "OK" : "MISMATCH");
    }
}