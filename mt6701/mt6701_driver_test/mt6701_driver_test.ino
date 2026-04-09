/*
 * MT6701_Driver Hardware Test
 *
 * Tests the new MT6701_Driver library with CRC6 validation,
 * filtering, and health monitoring.
 *
 * Wiring (Teensy 4.1 SPI1):
 *   MT6701 B (CLK) -> Pin 27 (SCK1)
 *   MT6701 A (DO)  -> Pin 1  (MISO1)
 *   MT6701 Z (CSN) -> Pin 0
 *   MT6701 VDD     -> 3.3V
 *   MT6701 GND     -> GND
 */

#include "MT6701_Driver.h"

MT6701_Driver encoder;

uint32_t loop_count = 0;
uint32_t last_print_ms = 0;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);

    Serial.println();
    Serial.println("=================================");
    Serial.println("MT6701_Driver Hardware Test");
    Serial.println("=================================");
    Serial.println();

    MT6701_Driver::Config cfg = {
        .cs_pins = {0},
        .num_encoders = 1,
        .spi_clock_hz = 4000000,
        .filter_alpha = 0.4f,
        .crc_fail_threshold = 10
    };

    if (!encoder.begin(cfg)) {
        Serial.println("ERROR: encoder.begin() failed!");
        while (1) delay(100);
    }

    Serial.println("Encoder initialized.");
    Serial.println("Rotate the magnet to see angle changes.");
    Serial.println();
    Serial.println("Commands: 'd' = diagnostics, 'v' = verbose diagnostics, 'r' = reset");
    Serial.println();
}

void loop() {
    // Read at ~1 kHz
    encoder.read(0);
    loop_count++;

    // Print at 10 Hz
    if (millis() - last_print_ms >= 100) {
        last_print_ms = millis();

        float angle_deg = encoder.getAngle(0) * MT6701_Driver::DEG_PER_RAD;
        float velocity = encoder.getVelocity(0) * MT6701_Driver::DEG_PER_RAD;
        MT6701_Driver::Health health = encoder.getHealth(0);

        Serial.print("Angle: ");
        if (angle_deg < 100) Serial.print(" ");
        if (angle_deg < 10) Serial.print(" ");
        Serial.print(angle_deg, 2);
        Serial.print(" deg | Vel: ");
        if (velocity >= 0) Serial.print(" ");
        Serial.print(velocity, 1);
        Serial.print(" deg/s | ");

        switch (health) {
            case MT6701_Driver::Health::OK:
                Serial.print("OK");
                break;
            case MT6701_Driver::Health::DEGRADED:
                Serial.print("DEGRADED");
                break;
            case MT6701_Driver::Health::FAULTED:
                Serial.print("FAULTED!");
                break;
        }

        Serial.print(" | loops: ");
        Serial.println(loop_count);
        loop_count = 0;
    }

    // Handle serial commands
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'd') {
            Serial.println();
            encoder.printDiagnostics(Serial, false);
            Serial.println();
        } else if (c == 'v') {
            Serial.println();
            encoder.printDiagnostics(Serial, true);
        } else if (c == 'r') {
            encoder.reset(0);
            Serial.println("Encoder reset.");
        }
    }

    delayMicroseconds(1000);  // ~1 kHz loop
}