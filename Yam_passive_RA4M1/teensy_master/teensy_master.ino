/*
 * Teensy 4.1 — CAN Encoder Network Master with structured telemetry.
 *
 * Polls XIAO RA4M1 encoder nodes via CAN bus, aggregates state through a
 * NodeState machine, emits ASCII key=value telemetry on USB Serial for the
 * Textual TUI in ../Textual_python/.
 *
 * Wire format / verbosity / control protocol: DOCS/Error_handling.md §5
 * Board: Teensy 4.1   CAN2 = pin 1 (TX) / pin 0 (RX) via TJA1051.
 */

#include <Arduino.h>
#include <FlexCAN_T4.h>
#include "config.h"
#include <can_protocol.h>
#include "debug_log.h"
#include "node_state.h"
#include "host_control.h"

//=============================================================================
// CAN bus
//=============================================================================
FlexCAN_T4<CAN2, RX_SIZE_256, TX_SIZE_16> can2;

//=============================================================================
// Direct FlexCAN2 ECR access — TEC/REC. Teensy 4.x maps CAN2 → FlexCAN2 base.
// TODO: replace with FlexCAN_T4 API method if/when it exposes ECR directly.
//=============================================================================
static inline uint8_t can2_tec() {
    return (*(volatile uint32_t*)0x401D401CUL) & 0xFF;
}
static inline uint8_t can2_rec() {
    return ((*(volatile uint32_t*)0x401D401CUL) >> 8) & 0xFF;
}

//=============================================================================
// Master-level telemetry
//=============================================================================
struct MasterTelemetry {
    uint32_t sync_tx, poll_tx, init_tx;
    uint32_t tx_mb_busy;
    uint32_t rx_valid, rx_unknown_id, rx_short_dlc, rx_mb_ovr;
    uint32_t cycle_count, cycle_slip;
    uint32_t cycle_jitter_max_us;
    uint8_t  last_tec, last_rec;
    uint8_t  bus_state;   // 0=ACTIVE 1=WARNING 2=PASSIVE 3=BUS_OFF
};
static MasterTelemetry g_tel = {};
static NodeTelemetry   g_nodes[NUM_NODES];

static uint32_t g_last_poll_ms        = 0;
static uint32_t g_last_summary_ms     = 0;
static uint32_t g_last_init_retry_ms  = 0;
static uint32_t g_last_cycle_us       = 0;

static const char* BUS_STATE_STR[] = { "ACTIVE", "WARNING", "PASSIVE", "BUS_OFF" };

//=============================================================================
// TX wrappers
//=============================================================================
static bool txFrame(uint16_t id, uint8_t len, const uint8_t* data) {
    CAN_message_t msg;
    msg.id = id;
    msg.len = len;
    msg.flags.extended = 0;
    for (uint8_t i = 0; i < len; i++) msg.buf[i] = data[i];
    if (can2.write(msg)) return true;
    g_tel.tx_mb_busy++;
    return false;
}

static bool txSync()       { if (txFrame(CAN_ID_SYNC,        FRAME_SIZE_SYNC,        nullptr)) { g_tel.sync_tx++; return true; } return false; }
static bool txPollSample() { if (txFrame(CAN_ID_POLL_SAMPLE, FRAME_SIZE_POLL_SAMPLE, nullptr)) { g_tel.poll_tx++; return true; } return false; }
static bool txInit()       { uint8_t d = INIT_FLAG_ENABLE; if (txFrame(CAN_ID_INIT, FRAME_SIZE_INIT, &d)) { g_tel.init_tx++; return true; } return false; }

//=============================================================================
// Bus-state derivation from TEC/REC (CAN 2.0B spec)
//=============================================================================
static uint8_t deriveBusState(uint8_t tec, uint8_t rec) {
    if (tec >= 255)              return 3;  // BUS_OFF
    if (tec >= 128 || rec >= 128) return 2;  // ERROR_PASSIVE
    if (tec >= 96  || rec >= 96)  return 1;  // WARNING
    return 0;                                // ERROR_ACTIVE
}

static void pollCanErrors() {
    const uint8_t tec = can2_tec();
    const uint8_t rec = can2_rec();
    const uint8_t st  = deriveBusState(tec, rec);

    if (tec != g_tel.last_tec || rec != g_tel.last_rec) {
        if (dbg_throttle_ok("ecnt", 1000)) {
            const Severity sev = (st >= 2) ? Severity::ERR : Severity::WARN;
            dbg_event(sev, EvtClass::BUS, 0,
                      "TEC %u->%u  REC %u->%u",
                      (unsigned)g_tel.last_tec, (unsigned)tec,
                      (unsigned)g_tel.last_rec, (unsigned)rec);
        }
        g_tel.last_tec = tec;
        g_tel.last_rec = rec;
    }
    if (st != g_tel.bus_state) {
        const Severity sev = (st == 3) ? Severity::FATAL
                           : (st >= 2) ? Severity::ERR
                                       : Severity::WARN;
        dbg_event(sev, EvtClass::BUS, 0, "state %s -> %s",
                  BUS_STATE_STR[g_tel.bus_state], BUS_STATE_STR[st]);
        g_tel.bus_state = st;
    }
}

//=============================================================================
// 1 Hz STAT + per-node NODE line emission
//=============================================================================
static void buildFlagsString(uint8_t status, char* fl, size_t flsize) {
    if (status == 0) { fl[0] = '-'; fl[1] = '\0'; return; }
    size_t off = 0;
    auto add = [&](bool cond, const char* s) {
        if (!cond) return;
        if (off > 0 && off < flsize - 1) fl[off++] = ',';
        while (*s && off < flsize - 1) fl[off++] = *s++;
    };
    add(status & STATUS_VALID,        "V");
    add(status & STATUS_STALE,        "S");
    add(status & STATUS_SENSOR_FAULT, "F");
    add(status & STATUS_FIELD_WEAK,   "Fw");
    add(status & STATUS_FIELD_STRONG, "Fs");
    add(status & STATUS_CRC_ERROR,    "C");
    add(status & STATUS_DEGRADED,     "D");
    add(status & STATUS_RECOVERING,   "R");
    fl[off] = '\0';
}

static void emitSummary(uint32_t now_ms) {
    char buf[224];
    snprintf(buf, sizeof(buf),
             "STAT t=%lu poll=%lu tec=%u rec=%u esr=0000 bus=%s "
             "sync=%lu polltx=%lu init=%lu mbb=%lu "
             "rxv=%lu rxu=%lu rxs=%lu ovr=%lu slip=%lu jit=%lu",
             (unsigned long)now_ms, (unsigned long)g_tel.cycle_count,
             (unsigned)g_tel.last_tec, (unsigned)g_tel.last_rec,
             BUS_STATE_STR[g_tel.bus_state],
             (unsigned long)g_tel.sync_tx, (unsigned long)g_tel.poll_tx,
             (unsigned long)g_tel.init_tx, (unsigned long)g_tel.tx_mb_busy,
             (unsigned long)g_tel.rx_valid, (unsigned long)g_tel.rx_unknown_id,
             (unsigned long)g_tel.rx_short_dlc, (unsigned long)g_tel.rx_mb_ovr,
             (unsigned long)g_tel.cycle_slip, (unsigned long)g_tel.cycle_jitter_max_us);
    dbg_emit_stat_line(buf);

    for (uint8_t i = 0; i < NUM_NODES; i++) {
        NodeTelemetry& n = g_nodes[i];
        const uint8_t nid = i + 1;
        if (n.state == NodeState::UNSEEN) {
            snprintf(buf, sizeof(buf),
                     "NODE id=%u st=%s rxs=0 rxt=0 miss=0 lat=- ang=- vel=- fl=-",
                     nid, nodeStateName(n.state));
        } else {
            char fl[16];
            buildFlagsString(n.status, fl, sizeof(fl));
            const float ang_deg = n.angle_rad * 57.2957795f;  // 180/π
            snprintf(buf, sizeof(buf),
                     "NODE id=%u st=%s rxs=%lu rxt=%lu miss=%u lat=%lu ang=%.1f vel=%.2f fl=%s",
                     nid, nodeStateName(n.state),
                     (unsigned long)n.rx_in_window, (unsigned long)n.rx_count,
                     (unsigned)n.consecutive_miss, (unsigned long)n.last_latency_us,
                     ang_deg, n.velocity_rad_s, fl);
        }
        dbg_emit_node_line(buf);
        n.rx_in_window = 0;
    }
}

//=============================================================================
// "RESET counters" command — zero telemetry, preserve discovery state.
//=============================================================================
static void resetCounters() {
    memset(&g_tel, 0, sizeof(g_tel));
    for (uint8_t i = 0; i < NUM_NODES; i++) {
        g_nodes[i].rx_count        = 0;
        g_nodes[i].rx_in_window    = 0;
        g_nodes[i].consecutive_rx  = 0;
        g_nodes[i].consecutive_miss = 0;
    }
}

//=============================================================================
// Setup
//=============================================================================
void setup() {
    Serial.begin(DEBUG_HOST_BAUD);
    while (!Serial && millis() < 3000);

    dbg_init();
    host_ctrl_init();
    host_ctrl_set_reset_counters_cb(resetCounters);

    can2.begin();
    can2.setBaudRate(CAN_BAUD);
    // NOTE: FIFO intentionally disabled. enableFIFO() without explicit filter
    // setup defaults to REJECT_ALL in FlexCAN_T4 and drops every frame.
    // Default MB mode auto-configures RX mailboxes with mask=0 (accept all).

    for (uint8_t i = 0; i < NUM_NODES; i++) {
        g_nodes[i] = {};
        g_nodes[i].state = NodeState::UNSEEN;
    }

    dbg_event(Severity::INFO, EvtClass::SYSTEM, 0,
              "master boot: nodes=%u baud=%lu poll=%lums",
              (unsigned)NUM_NODES, (unsigned long)CAN_BAUD, (unsigned long)POLL_PERIOD_MS);

    // Quick startup INIT burst. Continuous discovery in loop() handles the rest.
    for (uint8_t i = 0; i < 3; i++) { txInit(); delay(30); }

    g_last_poll_ms    = millis();
    g_last_summary_ms = millis();
    g_last_cycle_us   = micros();
}

//=============================================================================
// Loop
//=============================================================================
void loop() {
    host_ctrl_poll();

    const uint32_t now_ms = millis();

    if (now_ms - g_last_poll_ms >= POLL_PERIOD_MS) {
        // Cycle timing (slip/jitter) — measured from the previous cycle's start.
        const uint32_t cycle_us = micros();
        if (g_last_cycle_us != 0) {
            const uint32_t actual_us = cycle_us - g_last_cycle_us;
            const uint32_t budget_us = (uint32_t)POLL_PERIOD_MS * 1000UL;
            if (actual_us > budget_us) {
                const uint32_t jitter = actual_us - budget_us;
                if (jitter > g_tel.cycle_jitter_max_us) g_tel.cycle_jitter_max_us = jitter;
            }
            if (actual_us > budget_us * 2) {
                g_tel.cycle_slip++;
                if (dbg_throttle_ok("slip", 1000))
                    dbg_event(Severity::WARN, EvtClass::TIMING, 0,
                              "cycle slip - last poll %.1fms (budget %lums)",
                              actual_us / 1000.0f, (unsigned long)POLL_PERIOD_MS);
            }
        }
        g_last_cycle_us = cycle_us;
        g_last_poll_ms += POLL_PERIOD_MS;
        if (now_ms - g_last_poll_ms > POLL_PERIOD_MS * 2) g_last_poll_ms = now_ms;

        // Periodic INIT retry while any node is unseen or offline.
        bool any_missing = false;
        for (uint8_t i = 0; i < NUM_NODES; i++) {
            if (g_nodes[i].state == NodeState::UNSEEN || g_nodes[i].state == NodeState::OFFLINE) {
                any_missing = true; break;
            }
        }
        if (any_missing && (now_ms - g_last_init_retry_ms >= INIT_RETRY_MS)) {
            g_last_init_retry_ms = now_ms;
            txInit();
        }

        // SYNC + POLL — record SYNC TX timestamp for latency.
        const uint32_t sync_tx_us = micros();
        txSync();
        txPollSample();
        g_tel.cycle_count++;

        // RX window
        uint8_t rx_mask = 0;
        const uint32_t deadline = micros() + REPLY_TIMEOUT_US;
        while (micros() < deadline) {
            CAN_message_t msg;
            if (!can2.read(msg)) continue;

            if (msg.id >= CAN_ID_SAMPLE_BASE + 1 && msg.id <= CAN_ID_SAMPLE_BASE + NUM_NODES) {
                const uint8_t node_idx = msg.id - CAN_ID_SAMPLE_BASE - 1;
                if (msg.len < 7) {
                    g_tel.rx_short_dlc++;
                    if (dbg_throttle_ok("rx_short", 1000))
                        dbg_event(Severity::WARN, EvtClass::FRAME, node_idx + 1,
                                  "short DLC on ID 0x%03X (len=%u, expected 8)",
                                  (unsigned)msg.id, (unsigned)msg.len);
                    continue;
                }
                const uint16_t angle_raw = msg.buf[0] | (msg.buf[1] << 8);
                const int16_t  vel_raw   = msg.buf[2] | (msg.buf[3] << 8);
                const uint8_t  status    = msg.buf[6];
                const uint32_t latency_us = micros() - sync_tx_us;
                nodeOnReceive(g_nodes[node_idx], node_idx + 1,
                              decodeAngle(angle_raw), decodeVelocity(vel_raw), status,
                              now_ms, latency_us, g_tel.init_tx);
                rx_mask |= (1 << node_idx);
                g_tel.rx_valid++;
            } else {
                g_tel.rx_unknown_id++;
                if (dbg_throttle_ok("rx_unk", 1000))
                    dbg_event(Severity::WARN, EvtClass::FRAME, 0,
                              "unknown ID 0x%03X", (unsigned)msg.id);
            }
        }

        // Miss accounting for nodes that didn't reply this cycle.
        for (uint8_t i = 0; i < NUM_NODES; i++) {
            if (!(rx_mask & (1 << i))) nodeOnMiss(g_nodes[i], i + 1, now_ms);
        }

        pollCanErrors();
    }

    // 1 Hz summary
    if (now_ms - g_last_summary_ms >= DEBUG_PRINT_PERIOD_MS) {
        g_last_summary_ms = now_ms;
        emitSummary(now_ms);
    }
}
