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

    // Debug output at 1 Hz (encoder stats)
    #if DEBUG_ENABLED
    static uint32_t last_debug_ms = 0;
    if (millis() - last_debug_ms >= 1000) {
        last_debug_ms = millis();
        DBG.print("[ENC] ang=");
        DBG.print(angle_filtered_rad * 180.0f / PI, 1);
        DBG.print("° vel=");
        DBG.print(velocity_rad_s, 2);
        DBG.print(" rad/s crc_err=");
        DBG.println(total_crc_errors);
    }
    #endif
}

//=============================================================================
// Setup
//=============================================================================
void setup() {
    // Status LED first (diagnostic feedback)
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // Debug serial (USB or FTDI via Serial1/Serial2)
    DBG.begin(DBG_BAUD);
    #if DEBUG_SERIAL_PORT == 0
    while (!DBG && millis() < 500);  // Short wait for USB only
    #else
    delay(100);  // Brief delay for FTDI
    #endif

    DBG.println();
    DBG.println("=== XIAO RA4M1 CAN Encoder Node ===");
    DBG.print("[BOOT] Node ID: ");
    DBG.println(NODE_ID);
    DBG.print("[BOOT] Debug via ");
    DBG.println(DBG_PINS);

    // Phase 1: SPI init
    DBG.print("[INIT] SPI... ");
    mt6701_init();
    DBG.println("OK");

    // Phase 2: CAN init
    DBG.print("[INIT] CAN @ 1Mbps... ");
    if (!CAN.begin(CanBitRate::BR_1000k)) {
        DBG.println("FAILED!");
        DBG.println("[FATAL] CAN.begin() returned false");
        // Very fast blink = CAN init failed
        while (1) {
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
            delay(50);
        }
    }
    DBG.println("OK");

    // Phase 3: Summary
    DBG.println("[INIT] All peripherals initialized");
    DBG.print("[CFG] SPI: CS=D");
    DBG.print(SPI_CS_PIN);
    DBG.print(", SCK=D");
    DBG.print(SPI_SCK_PIN);
    DBG.print(", MISO=D");
    DBG.println(SPI_MISO_PIN);
    DBG.println("[CFG] CAN: D17(RX), D18(TX)");
    DBG.println();
    DBG.println("[RUN] Waiting for CAN frames...");
    DBG.println("=====================================");
    DBG.println();

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

    // Track last CAN RX for LED status (must be before LED block)
    static uint32_t last_can_rx_time = 0;

    // LED heartbeat: fast (200ms) = CAN connected, slow (1s) = not connected
    static uint32_t last_blink = 0;
    bool can_connected = (millis() - last_can_rx_time < 1000);
    uint32_t blink_period = can_connected ? 200 : 1000;

    if (millis() - last_blink >= blink_period) {
        last_blink = millis();
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }

    // CAN RX handling
    size_t avail = CAN.available();

    // Debug: structured status every second
    #if DEBUG_ENABLED
    static uint32_t last_can_debug = 0;
    if (millis() - last_can_debug >= 1000) {
        last_can_debug = millis();
        DBG.print("[STAT] SYNC=");
        DBG.print(sync_rx_count);
        DBG.print(" POLL=");
        DBG.print(poll_rx_count);
        DBG.print(" ang=");
        DBG.print(angle_filtered_rad * 180.0f / PI, 1);
        DBG.print("° CAN_avail=");
        DBG.print(avail);
        DBG.print(" connected=");
        DBG.println(can_connected ? "YES" : "NO");
    }
    #endif

    while (avail > 0) {
        CanMsg const msg = CAN.read();
        avail--;

        if (msg.id == CAN_ID_SYNC) {
            // SYNC received — trigger immediate sample
            sync_rx_count++;
            last_can_rx_time = millis();  // Track for LED status
            sampleEncoder(now_us);
        }
        else if (msg.id == CAN_ID_POLL_SAMPLE) {
            // POLL received — set flag for TX (M4)
            poll_rx_count++;
            last_can_rx_time = millis();  // Track for LED status
            poll_pending = true;
        }
        else if (msg.id == CAN_ID_INIT) {
            // M7: INIT received — respond immediately with SAMPLE_REPLY
            // This allows master to discover which nodes are connected
            last_can_rx_time = millis();  // Track for LED status
            poll_pending = true;
            #if DEBUG_ENABLED
            DBG.println("[RX] INIT received - responding");
            #endif
        }
    }

    // M4: CAN TX for SAMPLE_REPLY when poll_pending
    if (poll_pending) {
        poll_pending = false;

        // Build status byte
        uint8_t status = 0;
        if (filter_initialized && consecutive_fail < SENSOR_DEGRADED_THRESHOLD) {
            status |= STATUS_VALID;
        }
        if (consecutive_fail > 0) {
            status |= STATUS_CRC_ERROR;
        }

        // Encode angle and velocity
        uint16_t angle_raw = encodeAngle(angle_filtered_rad);
        int16_t vel_raw = encodeVelocity(velocity_rad_s);

        // Build 8-byte payload
        uint8_t data[8];
        data[0] = angle_raw & 0xFF;
        data[1] = (angle_raw >> 8) & 0xFF;
        data[2] = vel_raw & 0xFF;
        data[3] = (vel_raw >> 8) & 0xFF;
        data[4] = 0;  // reserved (accel LSB)
        data[5] = 0;  // reserved (accel MSB)
        data[6] = status;
        data[7] = 0;  // reserved

        // Send SAMPLE_REPLY
        CanMsg reply(CanStandardId(CAN_ID_SAMPLE_BASE + NODE_ID), 8, data);
        CAN.write(reply);  // Non-blocking; M5 tracks missing replies
    }
}
