/*
 * CAN Node — XIAO RA4M1
 *
 * Listens for POLL frames, responds with ACK.
 * LED toggles on each POLL (visual heartbeat).
 *
 * Hardware:
 *   XIAO RA4M1 native CAN: D17 (CAN_RX/P102), D18 (CAN_TX/P103)
 *   CAN Pal transceiver connected to D17/D18
 *   Termination: CAN Pal has onboard 120Ω (use switch)
 *
 * Protocol:
 *   POLL: ID=0x100, DLC=1, payload=[seq_num]
 *   ACK:  ID=0x200+NODE_ID, DLC=2, payload=[seq_num, status]
 *
 * Variant modified: pins_arduino.h changed to use D17/D18 for CAN
 * If board package updates, re-apply pin change to installed variant.
 */

#include <Arduino_CAN.h>
#include "config.h"

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);

    Serial.print("=== CAN Node ");
    Serial.print(NODE_ID);
    Serial.println(" (XIAO RA4M1) ===");
    Serial.print("ACK ID: 0x");
    Serial.println(CAN_ID_ACK_BASE + NODE_ID, HEX);

    if (!CAN.begin(CanBitRate::BR_1000k)) {
        Serial.println("CAN.begin FAILED!");
        pinMode(LED_BUILTIN, OUTPUT);
        while (1) {
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            delay(100);  // Fast blink = CAN init failed
        }
    }
    Serial.println("CAN OK");

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
}

static uint32_t g_rx_count = 0;
static uint32_t g_tx_fail_count = 0;
static uint32_t g_last_rx_count = 0;
static uint32_t g_last_print = 0;

void loop() {
    // Stats print every 1 second
    if (millis() - g_last_print >= 1000) {
        g_last_print = millis();
        uint32_t period_rx = g_rx_count - g_last_rx_count;
        Serial.print("RX: ");
        Serial.print(period_rx);
        Serial.print("/sec  Total: ");
        Serial.print(g_rx_count);
        if (g_tx_fail_count > 0) {
            Serial.print("  TX_FAIL: ");
            Serial.print(g_tx_fail_count);
        }
        Serial.println();
        g_last_rx_count = g_rx_count;
    }

    if (CAN.available()) {
        CanMsg const msg = CAN.read();

        if (msg.id == CAN_ID_POLL) {
            g_rx_count++;
            uint8_t seq = msg.data[0];

            // Send ACK
            uint8_t ack_data[2] = {seq, 0x00};  // seq + status OK
            CanMsg ack(CanStandardId(CAN_ID_ACK_BASE + NODE_ID), 2, ack_data);

            if (CAN.write(ack) < 0) {
                g_tx_fail_count++;
            }

            // Toggle LED on each POLL (visual heartbeat)
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        }
    }
}
