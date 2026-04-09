/*
 * MT6701 Multi-Encoder Test (2 encoders)
 *
 * Wiring (Teensy 4.1 SPI1):
 *   Shared:
 *     MT6701 B (CLK) -> Pin 27 (SCK1)
 *     MT6701 A (DO)  -> Pin 1  (MISO1)
 *
 *   Encoder 0: Z (CSN) -> Pin 0
 *   Encoder 1: Z (CSN) -> Pin 7
 *
 *   Both: VDD -> 3.3V, GND -> GND
 */

#include "MT6701_Driver.h"

MT6701_Driver encoders;

constexpr uint8_t NUM_ENCODERS = 2;
uint32_t loop_count = 0;
uint32_t last_print_ms = 0;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);

    Serial.println();
    Serial.println("=========================================");
    Serial.println("MT6701 Multi-Encoder Test (2 encoders)");
    Serial.println("=========================================");
    Serial.println();

    MT6701_Driver::Config cfg = {
        .cs_pins = {0, 7},
        .num_encoders = NUM_ENCODERS,
        .spi_clock_hz = 4000000,
        .filter_alpha = 0.4f,
        .crc_fail_threshold = 10
    };

    if (!encoders.begin(cfg)) {
        Serial.println("ERROR: encoders.begin() failed!");
        while (1) delay(100);
    }

    Serial.println("Encoders initialized.");
    Serial.println("  Encoder 0: CS pin 0");
    Serial.println("  Encoder 1: CS pin 7");
    Serial.println();
    Serial.println("Commands: 'd' = diagnostics, 'v' = verbose, 'r' = reset all");
    Serial.println();
}

void loop() {
    // Read all encoders at ~1 kHz
    encoders.readAll();
    loop_count++;

    // Print at 10 Hz
    if (millis() - last_print_ms >= 100) {
        last_print_ms = millis();

        for (uint8_t i = 0; i < NUM_ENCODERS; i++) {
            float angle_deg = encoders.getAngle(i) * MT6701_Driver::DEG_PER_RAD;
            float velocity = encoders.getVelocity(i) * MT6701_Driver::DEG_PER_RAD;
            MT6701_Driver::Health health = encoders.getHealth(i);

            Serial.print("E");
            Serial.print(i);
            Serial.print(": ");

            // Angle
            if (angle_deg < 100) Serial.print(" ");
            if (angle_deg < 10) Serial.print(" ");
            Serial.print(angle_deg, 1);
            Serial.print("deg ");

            // Velocity
            if (velocity >= 0) Serial.print(" ");
            if (abs(velocity) < 100) Serial.print(" ");
            if (abs(velocity) < 10) Serial.print(" ");
            Serial.print(velocity, 0);
            Serial.print("d/s ");

            // Health
            switch (health) {
                case MT6701_Driver::Health::OK:       Serial.print("OK "); break;
                case MT6701_Driver::Health::DEGRADED: Serial.print("DEG"); break;
                case MT6701_Driver::Health::FAULTED:  Serial.print("FLT"); break;
            }

            if (i < NUM_ENCODERS - 1) Serial.print(" | ");
        }

        Serial.print(" | lps:");
        Serial.println(loop_count);
        loop_count = 0;
    }

    // Handle serial commands
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'd') {
            Serial.println();
            encoders.printDiagnostics(Serial, false);
            Serial.println();
        } else if (c == 'v') {
            Serial.println();
            encoders.printDiagnostics(Serial, true);
        } else if (c == 'r') {
            for (uint8_t i = 0; i < NUM_ENCODERS; i++) {
                encoders.reset(i);
            }
            Serial.println("All encoders reset.");
        }
    }

    delayMicroseconds(1000);  // ~1 kHz loop
}
