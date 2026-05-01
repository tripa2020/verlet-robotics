/*
 * XIAO RA4M1 — CAN Encoder Node
 *
 * CAN-based encoder node for Yam_passive_RA4M1 network.
 * Reads MT6701 magnetic encoder via SPI, responds to CAN polls.
 *
 * Board: Seeed XIAO RA4M1
 * Select in Arduino IDE: Tools > Board > Seeed XIAO RA4M1
 *
 * Wiring:
 *   SPI:  D5 (CS), D8 (SCK), D9 (MISO) → MT6701
 *   CAN:  D17 (RX), D18 (TX) → TJA1051 transceiver
 *
 * Prerequisites:
 *   - Modified pins_arduino.h for CAN on D17/D18
 *   - Symlink shared/ to ~/Arduino/libraries/YamCanProtocol
 *
 * Ref: DOCS/Architecture.md
 */

#include <Arduino.h>
#include "config.h"
#include "mt6701.h"

#include <Arduino_CAN.h>
#include <can_protocol.h>

//=============================================================================
// Sampling State
//=============================================================================
static uint32_t last_sample_us = 0;
static uint32_t sample_count = 0;

// Filtered angle (EMA)
static float angle_filtered_rad = 0.0f;
static bool filter_initialized = false;

// Velocity calculation
static float prev_angle_rad = 0.0f;
static uint32_t prev_sample_us = 0;
static float velocity_rad_s = 0.0f;

// Fault tracking
static uint16_t consecutive_fail = 0;
static uint16_t consecutive_good = 0;
static uint32_t total_crc_errors = 0;

// CAN state
static uint32_t sync_rx_count = 0;
static uint32_t poll_rx_count = 0;
static bool poll_pending = false;  // Set by POLL RX, cleared after TX (M4)

//=============================================================================
// Angle wrap-around handling for velocity calculation
//=============================================================================
static float wrapDelta(float delta) {
    // Handle wrap-around at ±π boundary
    while (delta > PI) delta -= TWO_PI;
    while (delta < -PI) delta += TWO_PI;
    return delta;
}

//=============================================================================
// Sample MT6701 with filtering and velocity calculation
//=============================================================================
static void sampleEncoder(uint32_t now_us) {
    MT6701_Result result = mt6701_read();

    if (!result.read_ok || !result.crc_valid) {
        // Bad read - increment fault counter
        consecutive_fail++;
        consecutive_good = 0;
        if (!result.crc_valid && result.read_ok) {
            total_crc_errors++;
        }
        return;
    }

    // Good read
    consecutive_good++;
    if (consecutive_good > SENSOR_RECOVERY_THRESHOLD) {
        consecutive_fail = 0;
    }

    float raw_angle = result.angle_rad;

    // EMA filter
    if (!filter_initialized) {
        angle_filtered_rad = raw_angle;
        prev_angle_rad = raw_angle;
        prev_sample_us = now_us;
        filter_initialized = true;
    } else {
        angle_filtered_rad = EMA_ALPHA * raw_angle + (1.0f - EMA_ALPHA) * angle_filtered_rad;

        // Velocity calculation
        uint32_t dt_us = now_us - prev_sample_us;
        if (dt_us > 0) {
            float delta_angle = wrapDelta(raw_angle - prev_angle_rad);
            float dt_s = (float)dt_us * 1e-6f;
            velocity_rad_s = delta_angle / dt_s;
        }

        prev_angle_rad = raw_angle;
        prev_sample_us = now_us;
    }

    sample_count++;

    // Debug output at 1 Hz
    #if DEBUG_ENABLED
    static uint32_t last_debug_ms = 0;
    if (millis() - last_debug_ms >= 1000) {
        last_debug_ms = millis();
        Serial.print("N");
        Serial.print(NODE_ID);
        Serial.print(" | SYNC=");
        Serial.print(sync_rx_count);
        Serial.print(" | POLL=");
        Serial.print(poll_rx_count);
        Serial.print(" | ang=");
        Serial.print(angle_filtered_rad * 180.0f / PI, 1);
        Serial.print("° | vel=");
        Serial.print(velocity_rad_s, 2);
        Serial.print(" rad/s | crc_err=");
        Serial.println(total_crc_errors);
    }
    #endif
}

//=============================================================================
// Setup
//=============================================================================
void setup() {
    // USB serial for debug output
    Serial.begin(115200);
    while (!Serial && millis() < 3000);

    // Status LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // Initialize MT6701 SPI
    mt6701_init();

    // Initialize CAN bus
    if (!CAN.begin(CanBitRate::BR_1000k)) {
        Serial.println("CAN.begin FAILED!");
        while (1) {
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
            delay(100);  // Fast blink = CAN init failed
        }
    }

    // Print startup info
    Serial.println();
    Serial.println("=== XIAO RA4M1 CAN Encoder Node ===");
    Serial.print("Node ID: ");
    Serial.println(NODE_ID);
    Serial.print("Sample rate: ");
    Serial.print(1000000 / SAMPLE_PERIOD_US);
    Serial.println(" Hz");
    Serial.print("EMA alpha: ");
    Serial.println(EMA_ALPHA);
    Serial.print("SPI pins: CS=D");
    Serial.print(SPI_CS_PIN);
    Serial.print(", SCK=D");
    Serial.print(SPI_SCK_PIN);
    Serial.print(", MISO=D");
    Serial.println(SPI_MISO_PIN);
    Serial.println("CAN: 1 Mbps on D17 (RX), D18 (TX)");
    Serial.println();
    Serial.println("Setup complete. Waiting for SYNC...");
    Serial.println("====================================");
    Serial.println();

    // Initialize timing
    last_sample_us = micros();
}

//=============================================================================
// Loop
//=============================================================================
void loop() {
    uint32_t now_us = micros();

    // 500 Hz sampling (timer-triggered via polling)
    if (now_us - last_sample_us >= SAMPLE_PERIOD_US) {
        last_sample_us += SAMPLE_PERIOD_US;

        // Handle timing slip (if we fell behind, reset)
        if (now_us - last_sample_us > SAMPLE_PERIOD_US * 2) {
            last_sample_us = now_us;
        }

        sampleEncoder(now_us);
    }

    // LED heartbeat (blink at 1 Hz)
    static uint32_t last_blink = 0;
    if (millis() - last_blink >= 500) {
        last_blink = millis();
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }

    // CAN RX handling
    while (CAN.available()) {
        CanMsg const msg = CAN.read();

        if (msg.id == CAN_ID_SYNC) {
            // SYNC received — trigger immediate sample
            sync_rx_count++;
            sampleEncoder(now_us);
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));  // Toggle on SYNC
        }
        else if (msg.id == CAN_ID_POLL_SAMPLE) {
            // POLL received — set flag for TX (M4)
            poll_rx_count++;
            poll_pending = true;
        }
    }

    // TODO (Milestone 4): CAN TX for SAMPLE_REPLY when poll_pending
}
