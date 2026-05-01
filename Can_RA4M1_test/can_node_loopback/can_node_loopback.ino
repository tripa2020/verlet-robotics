/*
 * CAN Loopback Test — XIAO RA4M1
 *
 * Tests if Arduino_CAN library is working on this board.
 * No external wiring needed - internal loopback.
 */

#include <Arduino_CAN.h>

static uint32_t g_tx_count = 0;
static uint32_t g_rx_count = 0;
static uint32_t g_last_print = 0;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);

    Serial.println("=== XIAO RA4M1 CAN Loopback Test ===");

    // Try to initialize CAN
    if (!CAN.begin(CanBitRate::BR_1000k)) {
        Serial.println("CAN.begin FAILED!");
        pinMode(LED_BUILTIN, OUTPUT);
        while (1) {
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            delay(100);
        }
    }
    Serial.println("CAN.begin OK");

    // Enable internal loopback mode
    int loopback_result = CAN.enableInternalLoopback();
    Serial.print("enableInternalLoopback() returned: ");
    Serial.println(loopback_result);

    if (loopback_result == 1) {
        Serial.println("Loopback ENABLED");
    } else {
        Serial.print("Loopback FAILED with error: ");
        Serial.println(loopback_result);
        Serial.println("60001 = MODE_SWITCH_FAILED");
        Serial.println("60002 = INIT_FAILED");
    }

    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    // Send a test frame every 100ms
    static uint32_t last_tx = 0;
    if (millis() - last_tx >= 100) {
        last_tx = millis();

        uint8_t data[1] = {(uint8_t)(g_tx_count & 0xFF)};
        CanMsg msg(CanStandardId(0x100), 1, data);

        int rc = CAN.write(msg);
        if (rc >= 0) {
            g_tx_count++;
        } else {
            Serial.print("TX failed: ");
            Serial.println(rc);
        }
    }

    // Check for received frames
    if (CAN.available()) {
        CanMsg rx = CAN.read();
        g_rx_count++;
        Serial.print("RX! ID=0x");
        Serial.println(rx.id, HEX);
    }

    // Print stats every second
    if (millis() - g_last_print >= 1000) {
        g_last_print = millis();
        Serial.print("TX: ");
        Serial.print(g_tx_count);
        Serial.print("  RX: ");
        Serial.print(g_rx_count);
        Serial.println((g_rx_count == g_tx_count) ? "  OK" : "  MISMATCH");
    }
}