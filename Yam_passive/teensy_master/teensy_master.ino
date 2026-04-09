/*
 * Teensy 4.1 - Master Encoder Network Controller
 *
 * Polls XIAO RP2040 encoder nodes at 100 Hz and streams
 * aggregated data to host PC.
 *
 * Board: Teensy 4.1
 *
 * Node Wiring (Point-to-Point UART) - Teensy TX→XIAO RX(D7), Teensy RX←XIAO TX(D6):
 *   Node 1: Serial1  TX=1,  RX=0
 *   Node 2: Serial2  TX=8,  RX=7
 *   Node 3: Serial3  TX=14, RX=15
 *   Node 4: Serial4  TX=17, RX=16
 *   Node 5: Serial5  TX=20, RX=21
 *   Node 6: Serial6  TX=24, RX=25
 *   Node 7: Serial7  TX=29, RX=28
 *
 * Host Wiring:
 *   Serial8: TX=35, RX=34 → FTDI → PC
 *
 * USB Commands (Serial Monitor):
 *   s = Print detailed status
 *   d = Print diagnostics
 *   p = Pause/resume auto-display
 *   r = Reset statistics
 *   ? = Help
 *
 * Ref: Serial_master_node_architecture.md
 */

#include <Arduino.h>
#include <protocol.h>
#include "config.h"
#include "system_state.h"
#include "serial_interface.h"
#include "node_manager.h"
#include "fault_diagnostics.h"

//=============================================================================
// Global Objects
//=============================================================================
static SystemState      g_state;
static MultiUartBus     g_node_bus;
static UartHostStream   g_host_stream;
static NodeManager      g_manager;

//=============================================================================
// Task Timing
//=============================================================================
static uint32_t g_last_bus_poll_ms = 0;
static uint32_t g_last_host_tx_ms = 0;
static uint32_t g_last_diag_poll_ms = 0;
static uint32_t g_last_dashboard_ms = 0;
static bool g_auto_display_enabled = true;

//=============================================================================
// Status Flag Formatting (shared by printStatus and displayDashboard)
//=============================================================================
static void formatStatusFlags(uint8_t status, bool comm_fault, char* buf, size_t buflen) {
    buf[0] = '\0';
    if (comm_fault) strncat(buf, "COMM ", buflen - strlen(buf) - 1);
    if (statusIsFaulted(status)) strncat(buf, "SENS ", buflen - strlen(buf) - 1);
    if (statusIsStale(status)) strncat(buf, "STALE ", buflen - strlen(buf) - 1);
    if (statusIsFieldWeak(status)) strncat(buf, "WEAK ", buflen - strlen(buf) - 1);
    if (statusIsFieldStrong(status)) strncat(buf, "STRONG ", buflen - strlen(buf) - 1);
    if (statusHasCrcError(status)) strncat(buf, "CRC ", buflen - strlen(buf) - 1);
}

//=============================================================================
// Debug/Status Output
//=============================================================================
static void printStatus() {
    Serial.println("\n=== Master Status ===");
    Serial.printf("Frame count: %lu\n", g_state.frame_counter);
    Serial.printf("Nodes configured: %d, present: %d\n",
                  g_state.num_configured, g_state.num_present);
    Serial.printf("Poll stats: %lu total, %lu success, %lu timeout, %lu crc_err\n",
                  g_manager.getPollCount(), g_manager.getSuccessCount(),
                  g_manager.getTimeoutCount(), g_manager.getCrcErrorCount());

    for (uint8_t i = 0; i < g_state.num_configured; i++) {
        NodeState& node = g_state.nodes[i];
        float angle, velocity;
        uint8_t status;
        g_manager.getNodeOutput(node.node_id, angle, velocity, status);

        Serial.printf("\nNode %d: %s%s\n", node.node_id,
                      node.present ? "PRESENT" : "MISSING",
                      node.comm_faulted ? " [COMM FAULT]" : "");
        Serial.printf("  Angle:    %.4f rad (%.2f deg)\n", angle, angle * 180.0f / PI);
        Serial.printf("  Velocity: %.4f rad/s\n", velocity);
        Serial.printf("  Status:   0x%02X", status);
        if (statusIsValid(status)) Serial.print(" VALID");
        if (statusIsStale(status)) Serial.print(" STALE");
        if (statusIsFaulted(status)) Serial.print(" SENSOR_FAULT");
        if (statusIsFieldWeak(status)) Serial.print(" WEAK");
        if (statusIsFieldStrong(status)) Serial.print(" STRONG");
        Serial.println();
        Serial.printf("  Missed:   %u, last_rx: %lu us ago\n",
                      node.consecutive_missed,
                      micros() - node.last_rx_time_us);
    }
    Serial.println("=====================\n");
}

static void printDiagnostics() {
    Serial.println("\n=== Node Diagnostics ===");
    for (uint8_t i = 0; i < g_state.num_configured; i++) {
        NodeState& node = g_state.nodes[i];
        Serial.printf("\nNode %d:\n", node.node_id);
        Serial.printf("  Uptime:      %lu ms\n", node.uptime_ms);
        Serial.printf("  Sensor fail: %u\n", node.sensor_fail_count);
        Serial.printf("  UART errors: %u\n", node.uart_error_count);
        Serial.printf("  CRC errors:  %u\n", node.crc_error_count);
        Serial.printf("  Reset cause: 0x%02X\n", node.reset_cause);
    }
    Serial.println("========================\n");
}

static void printHelp() {
    Serial.println("\n--- Master Commands ---");
    Serial.println("  s = Print detailed status");
    Serial.println("  d = Print diagnostics");
    Serial.println("  p = Pause/resume auto-display");
    Serial.println("  r = Reset statistics");
    Serial.println("  ? = Help");
    Serial.println("-----------------------\n");
}

//=============================================================================
// Dashboard Display (Auto-updating at 1 Hz)
//=============================================================================
static void displayDashboard() {
    // Clear screen and move cursor to home (ANSI)
    Serial.print("\033[H\033[2J");

    // Header
    Serial.printf("--- YAM Master [100Hz | %luk frames] ---\n",
                  g_state.frame_counter / 1000);

    // Per-node status
    for (uint8_t i = 0; i < g_state.num_configured; i++) {
        NodeState& node = g_state.nodes[i];
        float angle, velocity;
        uint8_t status;
        g_manager.getNodeOutput(node.node_id, angle, velocity, status);

        // Status string
        const char* status_str;
        if (node.comm_faulted) {
            status_str = "FAULT";
        } else if (!node.present) {
            status_str = "MISS ";
        } else {
            status_str = "OK   ";
        }

        // Build flags string
        char flags[40];
        formatStatusFlags(status, node.comm_faulted, flags, sizeof(flags));

        // Print node line
        if (node.present && !node.comm_faulted) {
            Serial.printf("N%d: %s %7.2f° %+6.2f r/s  %s\n",
                          node.node_id, status_str,
                          angle * 180.0f / PI,
                          velocity,
                          flags);
        } else {
            Serial.printf("N%d: %s   ---     ---        %s\n",
                          node.node_id, status_str, flags);
        }
    }

    // Poll statistics
    uint32_t total = g_manager.getPollCount();
    uint32_t success = g_manager.getSuccessCount();
    float pct = (total > 0) ? (100.0f * success / total) : 0.0f;
    uint32_t errs = g_manager.getTimeoutCount() + g_manager.getCrcErrorCount();

    Serial.printf("Poll: %.1f%% (%lu/%lu) | Errs: %lu\n",
                  pct, success, total, errs);
    Serial.println("-----------------------------------------");
    Serial.println("[s]tatus [d]iag [p]ause [?]help");
}

static void processSerialCommands() {
    while (Serial.available()) {
        char c = Serial.read();
        switch (c) {
            case 's':
            case 'S':
                printStatus();
                break;
            case 'd':
            case 'D':
                printDiagnostics();
                break;
            case 'r':
            case 'R':
                // Reset would require adding reset methods to NodeManager
                Serial.println("Statistics reset not implemented");
                break;
            case 'p':
            case 'P':
                g_auto_display_enabled = !g_auto_display_enabled;
                if (g_auto_display_enabled) {
                    Serial.println("\n[Auto-display resumed]");
                } else {
                    Serial.println("\n[Auto-display paused - press 'p' to resume]");
                }
                break;
            case '?':
                printHelp();
                break;
        }
    }
}

//=============================================================================
// Bus Poll Task - Polls all nodes sequentially within 10ms budget
//=============================================================================
static void busPollTask() {
    // Poll each configured node
    for (uint8_t i = 0; i < g_state.num_configured; i++) {
        uint8_t node_id = g_state.nodes[i].node_id;

        // Send request
        g_manager.pollNode(node_id, CMD_GET_SAMPLE);

        // Process reply (blocking with timeout)
        g_manager.processReply(node_id, CMD_GET_SAMPLE);
    }

    // Update presence count after all polls
    systemState_updatePresenceCount(g_state);
}

//=============================================================================
// Host TX Task - Sends aggregated frame to PC
//=============================================================================
static void hostTxTask() {
    g_manager.sendHostFrame();
}

//=============================================================================
// Diagnostic Poll Task - Polls nodes for diagnostics (1 Hz)
//=============================================================================
static void diagPollTask() {
    // Poll one node per call, rotate through
    static uint8_t diag_node_index = 0;

    if (g_state.num_configured == 0) return;

    uint8_t node_id = g_state.nodes[diag_node_index].node_id;

    // Only poll if node is present
    NodeState* node = systemState_getNode(g_state, node_id);
    if (node != nullptr && node->present) {
        g_manager.pollNode(node_id, CMD_GET_DIAG);
        g_manager.processReply(node_id, CMD_GET_DIAG);
    }

    // Advance to next node
    diag_node_index++;
    if (diag_node_index >= g_state.num_configured) {
        diag_node_index = 0;
    }
}

//=============================================================================
// Setup
//=============================================================================
void setup() {
    // USB Serial for debug
    Serial.begin(115200);
    while (!Serial && millis() < 3000);

    Serial.println("\n====================================");
    Serial.println("Teensy 4.1 - Master Encoder Network");
    Serial.println("====================================");

    // Initialize system state
    systemState_init(g_state, NUM_CONFIGURED_NODES, NODE_IDS);

    // Configure node bus (point-to-point UARTs)
    HardwareSerial* ports[] = {
        &NODE1_SERIAL, &NODE2_SERIAL, &NODE3_SERIAL, &NODE4_SERIAL,
        &NODE5_SERIAL, &NODE6_SERIAL, &NODE7_SERIAL
    };
    g_node_bus.configure(ports, NODE_IDS, NUM_CONFIGURED_NODES, NODE_BAUD);
    g_node_bus.begin();

    // Configure host stream
    g_host_stream.configure(HOST_SERIAL, HOST_BAUD);
    g_host_stream.begin();

    // Initialize node manager
    g_manager.init(g_state, g_node_bus, g_host_stream);

    // Print configuration
    Serial.printf("Nodes configured: %d\n", NUM_CONFIGURED_NODES);
    for (uint8_t i = 0; i < NUM_CONFIGURED_NODES; i++) {
        Serial.printf("  Node %d\n", NODE_IDS[i]);
    }
    Serial.printf("Node baud: %lu\n", NODE_BAUD);
    Serial.printf("Host baud: %lu (Serial8 TX=%d RX=%d)\n",
                  HOST_BAUD, HOST_TX_PIN, HOST_RX_PIN);
    Serial.printf("Bus poll period: %lu ms (100 Hz)\n", BUS_POLL_PERIOD_MS);
    Serial.printf("Host TX period:  %lu ms (100 Hz)\n", HOST_TX_PERIOD_MS);
    Serial.printf("Diag period:     %lu ms (1 Hz)\n", DIAG_POLL_PERIOD_MS);

    Serial.println("\nReady. Type '?' for help.\n");

    // Initialize timing
    uint32_t now = millis();
    g_last_bus_poll_ms = now;
    g_last_host_tx_ms = now;
    g_last_diag_poll_ms = now;
    g_last_dashboard_ms = now;

    g_state.running = true;
}

//=============================================================================
// Loop - Cooperative Scheduler
//=============================================================================
void loop() {
    uint32_t now = millis();

    // Bus Poll Task (100 Hz - every 10 ms)
    if (now - g_last_bus_poll_ms >= BUS_POLL_PERIOD_MS) {
        g_last_bus_poll_ms = now;
        busPollTask();
    }

    // Host TX Task (100 Hz - every 10 ms, offset from bus poll)
    // Run after bus poll completes
    if (now - g_last_host_tx_ms >= HOST_TX_PERIOD_MS) {
        g_last_host_tx_ms = now;
        hostTxTask();
    }

    // Diagnostic Poll Task (1 Hz - every 1000 ms)
    if (now - g_last_diag_poll_ms >= DIAG_POLL_PERIOD_MS) {
        g_last_diag_poll_ms = now;
        diagPollTask();
    }

    // Process USB Serial commands
    processSerialCommands();

    // Dashboard Display Task (1 Hz)
    if (g_auto_display_enabled && (now - g_last_dashboard_ms >= STATUS_DISPLAY_PERIOD_MS)) {
        g_last_dashboard_ms = now;
        displayDashboard();
    }
}
