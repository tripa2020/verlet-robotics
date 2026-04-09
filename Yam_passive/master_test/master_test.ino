/*
 * Teensy 4.1 - Node Protocol Test
 *
 * Simple test sketch to verify node responds to binary protocol.
 * Sends GET_SAMPLE, GET_DIAG, PING and displays responses.
 *
 * Board: Teensy 4.1
 * Wiring:
 *   Teensy Pin 1 (TX1) → XIAO D7 (RX)
 *   Teensy Pin 0 (RX1) ← XIAO D6 (TX)
 *   GND ↔ GND
 *
 * Usage: Open Serial Monitor (115200), type:
 *   s = GET_SAMPLE
 *   d = GET_DIAG
 *   p = PING
 *   l = Loop polling (continuous)
 *   x = Stop loop
 */

#include <Arduino.h>
#include <protocol.h>  // YamProtocol library

#define NODE_ID         2
#define NODE_SERIAL     Serial1
#define NODE_BAUD       115200
#define RX_TIMEOUT_MS   50

static bool g_loop_mode = false;
static uint32_t g_last_poll = 0;
static uint32_t g_poll_count = 0;
static uint32_t g_success_count = 0;

//=============================================================================
// Send Request Frame
//=============================================================================
void sendRequest(uint8_t node_id, uint8_t cmd) {
    uint8_t frame[FRAME_SIZE_REQUEST];
    frame[0] = FRAME_START_REQUEST;
    frame[1] = node_id;
    frame[2] = cmd;
    frame[3] = crc8(frame, 3);

    NODE_SERIAL.write(frame, FRAME_SIZE_REQUEST);
}

//=============================================================================
// Read Response with Timeout
//=============================================================================
int readResponse(uint8_t* buffer, size_t expected_len, uint32_t timeout_ms) {
    uint32_t start = millis();
    size_t idx = 0;

    while (idx < expected_len && (millis() - start) < timeout_ms) {
        if (NODE_SERIAL.available()) {
            buffer[idx++] = NODE_SERIAL.read();
        }
    }
    return idx;
}

//=============================================================================
// GET_SAMPLE Command
//=============================================================================
void testGetSample(bool verbose) {
    uint8_t rx[FRAME_SIZE_SAMPLE];

    // Flush RX
    while (NODE_SERIAL.available()) NODE_SERIAL.read();

    sendRequest(NODE_ID, CMD_GET_SAMPLE);
    int len = readResponse(rx, FRAME_SIZE_SAMPLE, RX_TIMEOUT_MS);

    g_poll_count++;

    if (len != FRAME_SIZE_SAMPLE) {
        if (verbose) Serial.printf("[SAMPLE] Timeout: got %d bytes, expected %d\n", len, FRAME_SIZE_SAMPLE);
        return;
    }

    // Verify start byte and node ID
    if (rx[0] != FRAME_START_SAMPLE || rx[1] != NODE_ID) {
        if (verbose) Serial.printf("[SAMPLE] Bad header: 0x%02X 0x%02X\n", rx[0], rx[1]);
        return;
    }

    // Verify CRC
    uint8_t calc_crc = crc8(rx, 11);
    if (calc_crc != rx[11]) {
        if (verbose) Serial.printf("[SAMPLE] CRC fail: got 0x%02X, calc 0x%02X\n", rx[11], calc_crc);
        return;
    }

    g_success_count++;

    // Parse data
    float angle, velocity;
    memcpy(&angle, &rx[2], 4);
    memcpy(&velocity, &rx[6], 4);
    uint8_t status = rx[10];

    if (verbose) {
        Serial.println("\n[SAMPLE] OK");
        Serial.printf("  Angle:    %.4f rad (%.2f deg)\n", angle, angle * 180.0f / PI);
        Serial.printf("  Velocity: %.4f rad/s\n", velocity);
        Serial.printf("  Status:   0x%02X", status);
        if (status & STATUS_VALID) Serial.print(" VALID");
        if (status & STATUS_STALE) Serial.print(" STALE");
        if (status & STATUS_SENSOR_FAULT) Serial.print(" FAULT");
        if (status & STATUS_FIELD_WEAK) Serial.print(" WEAK");
        if (status & STATUS_FIELD_STRONG) Serial.print(" STRONG");
        Serial.println();
    } else {
        // Compact output for loop mode
        Serial.printf("%.2f deg, %.2f rad/s, 0x%02X\n", angle * 180.0f / PI, velocity, status);
    }
}

//=============================================================================
// GET_DIAG Command
//=============================================================================
void testGetDiag() {
    uint8_t rx[FRAME_SIZE_DIAGNOSTIC];

    while (NODE_SERIAL.available()) NODE_SERIAL.read();

    sendRequest(NODE_ID, CMD_GET_DIAG);
    int len = readResponse(rx, FRAME_SIZE_DIAGNOSTIC, RX_TIMEOUT_MS);

    if (len != FRAME_SIZE_DIAGNOSTIC) {
        Serial.printf("[DIAG] Timeout: got %d bytes, expected %d\n", len, FRAME_SIZE_DIAGNOSTIC);
        return;
    }

    if (rx[0] != FRAME_START_DIAGNOSTIC || rx[1] != NODE_ID) {
        Serial.printf("[DIAG] Bad header: 0x%02X 0x%02X\n", rx[0], rx[1]);
        return;
    }

    uint8_t calc_crc = crc8(rx, 14);
    if (calc_crc != rx[14]) {
        Serial.printf("[DIAG] CRC fail: got 0x%02X, calc 0x%02X\n", rx[14], calc_crc);
        return;
    }

    // Parse data
    uint32_t uptime;
    uint16_t sensor_fail, uart_err, crc_cnt;
    memcpy(&uptime, &rx[2], 4);
    memcpy(&sensor_fail, &rx[6], 2);
    memcpy(&uart_err, &rx[8], 2);
    uint8_t reset_cause = rx[10];
    memcpy(&crc_cnt, &rx[11], 2);

    Serial.println("\n[DIAG] OK");
    Serial.printf("  Uptime:      %lu ms\n", uptime);
    Serial.printf("  Sensor fail: %u\n", sensor_fail);
    Serial.printf("  UART errors: %u\n", uart_err);
    Serial.printf("  Reset cause: 0x%02X\n", reset_cause);
    Serial.printf("  CRC errors:  %u\n", crc_cnt);
}

//=============================================================================
// PING Command
//=============================================================================
void testPing() {
    uint8_t rx[FRAME_SIZE_PING];

    while (NODE_SERIAL.available()) NODE_SERIAL.read();

    sendRequest(NODE_ID, CMD_PING);
    int len = readResponse(rx, FRAME_SIZE_PING, RX_TIMEOUT_MS);

    if (len != FRAME_SIZE_PING) {
        Serial.printf("[PING] Timeout: got %d bytes, expected %d\n", len, FRAME_SIZE_PING);
        return;
    }

    if (rx[0] != FRAME_START_PING || rx[1] != NODE_ID) {
        Serial.printf("[PING] Bad header: 0x%02X 0x%02X\n", rx[0], rx[1]);
        return;
    }

    uint8_t calc_crc = crc8(rx, 3);
    if (calc_crc != rx[3]) {
        Serial.printf("[PING] CRC fail: got 0x%02X, calc 0x%02X\n", rx[3], calc_crc);
        return;
    }

    Serial.println("\n[PING] OK");
    Serial.printf("  Node ID: %d\n", rx[1]);
    Serial.printf("  Status:  0x%02X\n", rx[2]);
}

//=============================================================================
// Help
//=============================================================================
void printHelp() {
    Serial.println("\n--- Node Test Commands ---");
    Serial.println("  s = GET_SAMPLE");
    Serial.println("  d = GET_DIAG");
    Serial.println("  p = PING");
    Serial.println("  l = Loop mode (100 Hz polling)");
    Serial.println("  x = Stop loop");
    Serial.println("  ? = Help");
    Serial.println("--------------------------\n");
}

//=============================================================================
// Setup
//=============================================================================
void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);

    NODE_SERIAL.begin(NODE_BAUD);

    Serial.println("\nTeensy 4.1 - Node Protocol Test");
    Serial.printf("Testing Node %d on Serial1 @ %d baud\n", NODE_ID, NODE_BAUD);
    printHelp();
}

//=============================================================================
// Loop
//=============================================================================
void loop() {
    // Process USB commands
    while (Serial.available()) {
        char c = Serial.read();
        switch (c) {
            case 's':
            case 'S':
                testGetSample(true);
                break;
            case 'd':
            case 'D':
                testGetDiag();
                break;
            case 'p':
            case 'P':
                testPing();
                break;
            case 'l':
            case 'L':
                g_loop_mode = true;
                g_poll_count = 0;
                g_success_count = 0;
                Serial.println("\n--- Loop mode started (100 Hz) ---");
                break;
            case 'x':
            case 'X':
                g_loop_mode = false;
                Serial.printf("\n--- Loop stopped: %lu/%lu success (%.1f%%) ---\n",
                    g_success_count, g_poll_count,
                    g_poll_count > 0 ? (100.0f * g_success_count / g_poll_count) : 0.0f);
                break;
            case '?':
                printHelp();
                break;
        }
    }

    // Loop mode: poll at 100 Hz
    if (g_loop_mode && (millis() - g_last_poll >= 10)) {
        g_last_poll = millis();
        testGetSample(false);
    }
}