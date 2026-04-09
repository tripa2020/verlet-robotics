/*
 * Seeed XIAO RP2040 - MT6701 Encoder Node
 *
 * Binary protocol node for encoder network.
 * - Samples MT6701 at 500 Hz (timer-driven)
 * - EMA filter + velocity estimation
 * - Responds to binary protocol commands from master
 *
 * Board: Seeed XIAO RP2040
 * Select in Arduino IDE: Tools > Board > Seeed XIAO RP2040
 *
 * Wiring:
 *   UART: D6 (TX) → Teensy RX, D7 (RX) ← Teensy TX
 *   SPI:  D5 (CS), D8 (SCK), D9 (MISO) → MT6701
 *
 * Ref: Serial_master_node_architecture.md
 */

//=============================================================================
// DEBUG FLAG - set to true to enable USB Serial debug output
//=============================================================================
#define DEBUG_ENABLED       true
#define DEBUG_SAMPLE_INTERVAL 500   // print every N samples (500 = 1 Hz at 500 Hz sampling)

#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "mt6701.h"
#include <protocol.h>  // YamProtocol library (symlinked from shared/)

//=============================================================================
// Local Sample State (per architecture doc Section 5.5)
//=============================================================================
struct LocalSampleState {
    // Current sample (output)
    float    angle_rad;
    float    velocity_rad_s;
    uint8_t  status_flags;

    // Filtering
    float    ema_angle;
    float    prev_angle;
    uint32_t prev_sample_time_us;
    bool     first_sample;

    // Fault tracking
    uint16_t consecutive_sensor_fail;
    bool     sensor_faulted;
    float    last_valid_angle;
    float    last_valid_velocity;

    // Diagnostics
    uint32_t boot_time_ms;
    uint16_t total_crc_errors;
    uint16_t uart_error_count;

    // Timer flag
    volatile bool sample_due;

    // Debug counters
    uint32_t sample_count;
    uint32_t cmd_count;
};

static LocalSampleState g_state;

//=============================================================================
// RX Buffer for binary protocol
//=============================================================================
static uint8_t g_rx_buffer[16];
static uint8_t g_rx_index = 0;

//=============================================================================
// Timer ISR (RP2040 alarm)
//=============================================================================
static repeating_timer_t g_sample_timer;

static bool sampleTimerCallback(repeating_timer_t* rt) {
    g_state.sample_due = true;
    return true;  // keep repeating
}

//=============================================================================
// Sample Processing
//=============================================================================
static void processSample() {
    MT6701_Result result = mt6701_read();
    uint32_t now_us = micros();
    g_state.sample_count++;

    // Debug: print raw read result periodically
    #if DEBUG_ENABLED
    if (g_state.sample_count % DEBUG_SAMPLE_INTERVAL == 0) {
        Serial.printf("[DBG] #%lu raw=%u (%.2f deg) crc=%s field=%d\n",
            g_state.sample_count,
            result.raw_angle,
            result.angle_rad * 180.0f / PI,
            result.crc_valid ? "ok" : "FAIL",
            result.field_status);
    }
    #endif

    if (!result.read_ok) {
        // SPI transfer failed
        g_state.consecutive_sensor_fail++;
        #if DEBUG_ENABLED
        if (g_state.consecutive_sensor_fail <= 5 || g_state.consecutive_sensor_fail % 100 == 0) {
            Serial.printf("[DBG] SPI READ FAIL #%u\n", g_state.consecutive_sensor_fail);
        }
        #endif
        if (g_state.consecutive_sensor_fail >= SENSOR_FAULT_THRESHOLD) {
            g_state.sensor_faulted = true;
            statusSetFaulted(g_state.status_flags);
        }
        statusClearValid(g_state.status_flags);
        statusSetStale(g_state.status_flags);
        return;
    }

    // Check CRC
    if (!result.crc_valid) {
        g_state.total_crc_errors++;
        g_state.consecutive_sensor_fail++;
        #if DEBUG_ENABLED
        if (g_state.total_crc_errors <= 5 || g_state.total_crc_errors % 100 == 0) {
            Serial.printf("[DBG] CRC FAIL #%u (raw=%u)\n", g_state.total_crc_errors, result.raw_angle);
        }
        #endif
        statusSetCrcError(g_state.status_flags);
        if (g_state.consecutive_sensor_fail >= SENSOR_FAULT_THRESHOLD) {
            g_state.sensor_faulted = true;
            statusSetFaulted(g_state.status_flags);
        }
        statusClearValid(g_state.status_flags);
        statusSetStale(g_state.status_flags);
        return;
    }

    // Good read - reset fail counter
    g_state.consecutive_sensor_fail = 0;
    statusClearAll(g_state.status_flags);
    statusSetValid(g_state.status_flags);

    // Update field status flags
    if (result.field_status == MT6701_FIELD_WEAK) {
        statusSetFieldWeak(g_state.status_flags);
    } else if (result.field_status == MT6701_FIELD_STRONG) {
        statusSetFieldStrong(g_state.status_flags);
    }

    // EMA filter on angle
    float raw_angle = result.angle_rad;
    if (g_state.first_sample) {
        g_state.ema_angle = raw_angle;
        g_state.prev_angle = raw_angle;
        g_state.prev_sample_time_us = now_us;
        g_state.first_sample = false;
        g_state.velocity_rad_s = 0.0f;
    } else {
        // Handle wrap-around before EMA
        float delta = raw_angle - g_state.ema_angle;
        if (delta > 3.14159f) delta -= TWO_PI;
        if (delta < -3.14159f) delta += TWO_PI;

        // Apply EMA: y[n] = alpha * x[n] + (1-alpha) * y[n-1]
        g_state.ema_angle += EMA_ALPHA * delta;

        // Wrap EMA result to [0, 2*pi)
        if (g_state.ema_angle >= TWO_PI) g_state.ema_angle -= TWO_PI;
        if (g_state.ema_angle < 0.0f) g_state.ema_angle += TWO_PI;

        // Calculate velocity from filtered angle
        float angle_delta = g_state.ema_angle - g_state.prev_angle;
        if (angle_delta > 3.14159f) angle_delta -= TWO_PI;
        if (angle_delta < -3.14159f) angle_delta += TWO_PI;

        float dt = (float)(now_us - g_state.prev_sample_time_us) * 1e-6f;
        if (dt > 0.0f && dt < 0.1f) {  // sanity check dt
            g_state.velocity_rad_s = angle_delta / dt;
        }

        g_state.prev_angle = g_state.ema_angle;
        g_state.prev_sample_time_us = now_us;
    }

    // Update output
    g_state.angle_rad = g_state.ema_angle;
    g_state.last_valid_angle = g_state.angle_rad;
    g_state.last_valid_velocity = g_state.velocity_rad_s;
}

//=============================================================================
// Protocol: Build Sample Reply (12 bytes)
//=============================================================================
static void sendSampleReply() {
    uint8_t frame[FRAME_SIZE_SAMPLE];
    float angle, velocity;

    // Use frozen values if faulted
    if (g_state.sensor_faulted) {
        angle = g_state.last_valid_angle;
        velocity = g_state.last_valid_velocity;
    } else {
        angle = g_state.angle_rad;
        velocity = g_state.velocity_rad_s;
    }

    frame[0] = FRAME_START_SAMPLE;
    frame[1] = NODE_ID;

    // Pack angle (float32 LE)
    memcpy(&frame[2], &angle, 4);

    // Pack velocity (float32 LE)
    memcpy(&frame[6], &velocity, 4);

    // Status flags
    frame[10] = g_state.status_flags;

    // CRC8 over bytes 0-10
    frame[11] = crc8(frame, 11);

    Serial1.write(frame, FRAME_SIZE_SAMPLE);
}

//=============================================================================
// Protocol: Build Diagnostic Reply (15 bytes)
//=============================================================================
static void sendDiagReply() {
    uint8_t frame[FRAME_SIZE_DIAGNOSTIC];

    uint32_t uptime = millis() - g_state.boot_time_ms;

    frame[0] = FRAME_START_DIAGNOSTIC;
    frame[1] = NODE_ID;

    // Uptime (uint32 LE)
    memcpy(&frame[2], &uptime, 4);

    // Sensor fail count (uint16 LE)
    memcpy(&frame[6], &g_state.consecutive_sensor_fail, 2);

    // UART error count (uint16 LE)
    memcpy(&frame[8], &g_state.uart_error_count, 2);

    // Reset cause (v2 deferred - always UNKNOWN)
    frame[10] = RESET_UNKNOWN;

    // CRC error count (uint16 LE)
    memcpy(&frame[11], &g_state.total_crc_errors, 2);

    // Reserved
    frame[13] = 0x00;

    // CRC8 over bytes 0-13
    frame[14] = crc8(frame, 14);

    Serial1.write(frame, FRAME_SIZE_DIAGNOSTIC);
}

//=============================================================================
// Protocol: Build Ping Reply (4 bytes)
//=============================================================================
static void sendPingReply() {
    uint8_t frame[FRAME_SIZE_PING];

    frame[0] = FRAME_START_PING;
    frame[1] = NODE_ID;
    frame[2] = g_state.status_flags;
    frame[3] = crc8(frame, 3);

    Serial1.write(frame, FRAME_SIZE_PING);
}

//=============================================================================
// Protocol: Parse Request Frame
//=============================================================================
static void processRequest() {
    // Validate frame
    if (g_rx_index != FRAME_SIZE_REQUEST) {
        g_state.uart_error_count++;
        return;
    }

    // Check start byte
    if (g_rx_buffer[0] != FRAME_START_REQUEST) {
        g_state.uart_error_count++;
        return;
    }

    // Check node ID
    if (g_rx_buffer[1] != NODE_ID) {
        return;  // not for us, ignore
    }

    // Verify CRC
    uint8_t calc_crc = crc8(g_rx_buffer, 3);
    if (calc_crc != g_rx_buffer[3]) {
        g_state.uart_error_count++;
        return;
    }

    // Process command
    uint8_t cmd = g_rx_buffer[2];
    g_state.cmd_count++;

    #if DEBUG_ENABLED
    const char* cmd_name = "???";
    switch (cmd) {
        case CMD_GET_SAMPLE: cmd_name = "SAMPLE"; break;
        case CMD_GET_DIAG:   cmd_name = "DIAG"; break;
        case CMD_PING:       cmd_name = "PING"; break;
        case CMD_ZERO:       cmd_name = "ZERO"; break;
    }
    Serial.printf("[DBG] RX cmd=%s angle=%.2f vel=%.2f status=0x%02X\n",
        cmd_name, g_state.angle_rad * 180.0f / PI, g_state.velocity_rad_s, g_state.status_flags);
    #endif

    switch (cmd) {
        case CMD_GET_SAMPLE:
            sendSampleReply();
            break;
        case CMD_GET_DIAG:
            sendDiagReply();
            break;
        case CMD_PING:
            sendPingReply();
            break;
        case CMD_ZERO:
            // TODO: implement zero command in v2
            break;
        default:
            g_state.uart_error_count++;
            break;
    }
}

//=============================================================================
// UART RX Processing
//=============================================================================
static void processUartRx() {
    while (Serial1.available()) {
        uint8_t b = Serial1.read();

        // Look for start byte if buffer empty
        if (g_rx_index == 0) {
            if (b == FRAME_START_REQUEST) {
                g_rx_buffer[g_rx_index++] = b;
            }
            // else discard
        } else {
            g_rx_buffer[g_rx_index++] = b;

            // Check if we have a complete frame
            if (g_rx_index >= FRAME_SIZE_REQUEST) {
                processRequest();
                g_rx_index = 0;
            }
        }

        // Overflow protection
        if (g_rx_index >= sizeof(g_rx_buffer)) {
            g_rx_index = 0;
            g_state.uart_error_count++;
        }
    }
}

//=============================================================================
// Setup
//=============================================================================
void setup() {
    // Record boot time
    g_state.boot_time_ms = millis();

    // Initialize state
    memset(&g_state, 0, sizeof(g_state));
    g_state.boot_time_ms = millis();
    g_state.first_sample = true;

    // LED for visual feedback
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // USB serial for debugging
    Serial.begin(115200);

    // UART to master
    Serial1.setTX(UART_TX_PIN);
    Serial1.setRX(UART_RX_PIN);
    Serial1.begin(UART_BAUD);

    // Initialize MT6701
    mt6701_init();

    // Start 500 Hz sample timer (2000 us period, negative = exact)
    add_repeating_timer_us(-SAMPLE_PERIOD_US, sampleTimerCallback, nullptr, &g_sample_timer);

    // Debug output
    Serial.println("\n=== XIAO RP2040 Encoder Node ===");
    Serial.printf("Node ID: %d\n", NODE_ID);
    Serial.printf("Sample rate: %d Hz\n", 1000000 / SAMPLE_PERIOD_US);
    Serial.printf("UART: TX=GPIO%d, RX=GPIO%d @ %d\n", UART_TX_PIN, UART_RX_PIN, UART_BAUD);
    Serial.printf("SPI:  CS=GPIO%d, SCK=GPIO%d, MISO=GPIO%d\n", SPI_CS_PIN, SPI_SCK_PIN, SPI_MISO_PIN);
    #if DEBUG_ENABLED
    Serial.println("DEBUG: ENABLED (prints every " + String(DEBUG_SAMPLE_INTERVAL) + " samples)");
    #else
    Serial.println("DEBUG: disabled");
    #endif
    Serial.println("================================\n");
}

//=============================================================================
// Loop
//=============================================================================
void loop() {
    // Process sample if due (set by timer ISR)
    if (g_state.sample_due) {
        g_state.sample_due = false;
        processSample();
    }

    // Process UART commands
    processUartRx();
}