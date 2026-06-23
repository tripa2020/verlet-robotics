# Error Handling & Debug Telemetry — Yam_passive_RA4M1

## Document Status
- **Version:** 1.0
- **Date:** 2026-05-28
- **Status:** Implementation Plan
- **Owner:** Alex
- **Sister doc:** [Architecture.md](Architecture.md)

---

# PART 1 — ARCHITECTURE & REASONING

This part is human-readable and explains *why* the system is shaped this way.
Part 2 (below) is the machine-readable handoff for a fresh Claude session.

---

## 1. Motivation

The Teensy master currently reports 2 nodes when 4 are physically connected, and there is no way to tell whether the missing nodes are:

- never discovered (boot-window race), or
- discovered but silent (CAN/protocol failure), or
- replying but filtered out by the debug printer.

Existing debug is a single `Serial.printf` line per cycle that **only prints active nodes** — successful RX from undiscovered nodes is captured into `g_nodes[]` but invisible to the operator. There are no per-cycle RX counters, no CAN error-counter readout, no unknown-ID logging, no recovery from missed discovery, and no way to change verbosity without reflashing.

This document specifies a structured, runtime-configurable telemetry system: ASCII key=value lines on USB Serial consumed by a Textual TUI on the host PC.

---

## 2. Design principles

1. **Bounded output volume.** Periodic summary at 1 Hz; events fire only on state changes. No `printf` inside the 100 Hz hot loop except event-triggered.
2. **Severity prefix on every non-table line.** `[INFO]`, `[WARN]`, `[ERR ]`, `[FATAL]` — five chars, eye-scannable.
3. **Timestamps where they matter.** Every event carries `t=NNNN ms`. Periodic STAT/NODE lines carry the current `t`.
4. **Tabular when comparable.** Per-node data is one line per node with fixed key positions; humans pattern-match rows fast in the TUI.
5. **Hierarchy.** Bus health → TX/RX totals → per-node state → recent events. Same shape every second.
6. **Runtime verbosity control.** Python sends `SET level=N` over the same USB Serial; no reflash required.
7. **Plain ASCII on the wire, rich UI in Python.** The MCU emits `key=value` lines. All color, layout, and live update lives in the host-side Textual app.
8. **Failure-mode separation.** Errors are classified into six classes so the operator knows *where to look*, not just *that something broke*.

---

## 3. Master-side error taxonomy

Six classes, four severities. Every event the master emits maps to exactly one `(class, severity)` pair.

### 3.1 Classes

| Class | What it means | Where to look first |
|---|---|---|
| **BUS** | CAN physical layer / controller state — TEC, REC, ESR1, bus-off | Wiring, termination, baud, transceiver |
| **TX** | Master cannot transmit — mailbox busy, aborted | Bus back-pressure, scheduler overload |
| **NODE** | Per-node connectivity or sensor health | Power/wiring on that node, encoder magnet |
| **FRAME** | Protocol violation — unknown ID, wrong DLC, decode failure | Firmware version skew, corruption |
| **TIMING** | Master `loop()` slip — cycle took > `POLL_PERIOD_MS` | Profile master, reduce work in hot path |
| **SYSTEM** | RX pipeline overruns, driver-level loss | Buffer sizing, polling rate, library bugs |

### 3.2 Severities

| Level | Meaning | Action |
|---|---|---|
| **INFO** | Cosmetic state change (e.g. first discovery, state transition to ONLINE) | None — context only |
| **WARN** | Recoverable anomaly (e.g. 3 missed replies, transient TEC bump) | Watch; investigate if persistent |
| **ERR** | Persistent fault (e.g. node OFFLINE after 100 misses, ERROR_PASSIVE) | Investigate now |
| **FATAL** | System cannot continue (e.g. BUS_OFF) | Manual intervention required |

### 3.3 Example event lines

```
[INFO ] NODE   Node 4 first reply at t=4523ms after 4 INIT retries
[WARN ] NODE   Node 6: 3 consecutive misses (lat budget 410us)
[ERR  ] NODE   Node 3: OFFLINE — 100 consecutive misses
[WARN ] BUS    TEC stepped 0->4 (transient bit/stuff error)
[ERR  ] BUS    state ERROR_ACTIVE -> ERROR_PASSIVE
[FATAL] BUS    state BUS_OFF — controller halted TX
[WARN ] TX     mailbox busy x3 in last 1s
[WARN ] FRAME  unknown ID 0x080 (12 frames in last 1s)
[WARN ] FRAME  short DLC on ID 0x205 (len=4, expected 8)
[WARN ] TIMING cycle slip — last poll 12.4ms (budget 10ms)
[ERR  ] SYSTEM MB overrun x7 in last 1s
```

These are the only event phrasings the system emits. New phrasings require explicit additions — keeps the operator's mental model bounded.

---

## 4. NodeState machine

Each node lives in one of five states. Transitions drive `[INFO]`/`[WARN]`/`[ERR]` events.

```
                    boot
                     │
                     ▼
                ┌─────────┐
                │ UNSEEN  │  never replied
                └────┬────┘
                     │ first valid SAMPLE_REPLY
                     ▼
                ┌──────────┐
                │DISCOVERED│  first reply seen
                └────┬─────┘
                     │ 10 consecutive cycles received
                     ▼
                ┌──────────┐         miss_count >= 3
                │  ONLINE  │ ────────────────────────┐
                └──────────┘                         │
                     ▲                               ▼
                     │ rx OK                  ┌──────────┐
                     └────────────────────────│ MISSING  │
                                              └────┬─────┘
                                                   │ miss_count >= 100
                                                   ▼
                                              ┌──────────┐
                                              │ OFFLINE  │
                                              └────┬─────┘
                                                   │ valid SAMPLE_REPLY arrives
                                                   ▼
                                              ┌──────────┐
                                              │DISCOVERED│  (recurrence)
                                              └──────────┘
```

Events emitted on transitions:

| From → To | Severity | Class | Phrasing |
|---|---|---|---|
| `UNSEEN → DISCOVERED` | INFO | NODE | `Node N first reply at t=NNms after K INIT retries` |
| `DISCOVERED → ONLINE` | INFO | NODE | `Node N stable (ONLINE)` |
| `ONLINE → MISSING` | WARN | NODE | `Node N: 3 consecutive misses` |
| `MISSING → OFFLINE` | ERR | NODE | `Node N: OFFLINE — 100 consecutive misses` |
| `MISSING → ONLINE` | INFO | NODE | `Node N recovered` |
| `OFFLINE → DISCOVERED` | INFO | NODE | `Node N reappeared` |

`MISSING → ONLINE` requires the same 10-cycle stability gate as `DISCOVERED → ONLINE`.

---

## 5. Wire format

ASCII, line-delimited, type-tagged, key=value.

### 5.1 Master → Python (telemetry)

Three line types. Each starts with a 4-char tag in the first column.

#### STAT — 1 Hz bus & master summary
```
STAT t=12345 poll=1234 tec=0 rec=0 esr=0000 bus=ACTIVE sync=1234 polltx=1234 init=12 mbb=0 rxv=4920 rxu=0 rxs=0 ovr=0 slip=0 jit=23
```
| Key | Meaning |
|---|---|
| `t` | master uptime ms |
| `poll` | poll cycle count |
| `tec`, `rec` | CAN TX/RX error counters |
| `esr` | ESR1 register hex |
| `bus` | `ACTIVE` / `PASSIVE` / `BUS_OFF` |
| `sync`, `polltx`, `init` | TX counters (lifetime) |
| `mbb` | TX mailbox-busy count |
| `rxv`, `rxu`, `rxs`, `ovr` | RX valid / unknown-id / short-DLC / MB overrun (lifetime) |
| `slip`, `jit` | cycle slip count, max jitter in µs |

#### NODE — 1 Hz, one line per node (always 7 lines)
```
NODE id=1 st=ONLINE rxs=100 rxt=12345 miss=0 lat=180 ang=45.2 vel=0.05 fl=V
NODE id=3 st=MISSING rxs=0 rxt=0 miss=100 lat=- ang=- vel=- fl=-
```
| Key | Meaning |
|---|---|
| `id` | node id 1-7 |
| `st` | NodeState name |
| `rxs` | RX count this 1-second window |
| `rxt` | RX count lifetime |
| `miss` | consecutive miss count |
| `lat` | last latency SYNC→reply in µs (`-` if no data) |
| `ang` | filtered angle in degrees |
| `vel` | velocity in rad/s |
| `fl` | flag letters: `V`=VALID, `S`=STALE, `F`=SENSOR_FAULT, `Fw`=FIELD_WEAK, `Fs`=FIELD_STRONG, `C`=CRC_ERROR, `D`=DEGRADED, `R`=RECOVERING. `-` if no data. |

#### EVT — emitted immediately on state change
```
EVT  t=4523 sev=INFO cls=NODE node=4 msg="first reply after 4 INIT retries"
EVT  t=12100 sev=WARN cls=BUS msg="TEC 0->4"
```
| Key | Meaning |
|---|---|
| `t` | master uptime ms |
| `sev` | `INFO` / `WARN` / `ERR` / `FATAL` |
| `cls` | `BUS` / `TX` / `NODE` / `FRAME` / `TIMING` / `SYSTEM` |
| `node` | optional, node id if class=NODE |
| `msg` | free-form quoted string |

### 5.2 Python → Master (control)

Single-line ASCII commands, `\n` terminated. Master parses one command per `loop()` iteration via a 64-byte line buffer.

```
SET level=2          → set verbosity (0=silent, 1=events, 2=summary, 3=trace)
RESET counters       → zero all telemetry counters
RESET events         → clear event ring buffer
PING                 → master responds with PONG t=NNN
DUMP config          → master prints current config keys
```

### 5.3 Master → Python (acknowledgments)

Every command produces exactly one response line:

```
ACK level=2
ACK reset=counters
ACK reset=events
PONG t=12345
NAK cmd="unrecognized" reason="parse error"
```

### 5.4 Verbosity levels

| Level | Emits |
|---|---|
| 0 | Nothing |
| 1 | EVT lines only (always includes ERR and FATAL regardless of level) |
| 2 | EVT + STAT + NODE lines (default at boot) |
| 3 | Level 2 + per-cycle `RX t=NN id=0x2NN lat=NN` trace lines (costly) |

---

## 6. Python TUI architecture

```
Yam_passive_RA4M1/Textual_python/
├── main.py              ← Textual App; wires reader + state + widgets + bindings
├── state.py             ← @dataclass LiveState, BusStats, NodeRow, EventEntry. Pure data + @property.
├── protocol.py          ← Wire format constants, parse_line(), command builders. Pure functions.
├── serial_reader.py     ← Threaded serial I/O, port auto-detect, line buffering, command TX.
├── widgets.py           ← Textual widgets (BusPanel, NodesPanel, EventsPanel, StatusBar).
├── yam.tcss             ← Textual stylesheet (colors, borders, sizing)
└── README.md            ← Run instructions + keybinds
```

**Strict separation:**
- `widgets.py` imports from `state` / `protocol` only — never from `serial_reader`.
- `serial_reader.py` never builds widgets.
- I/O, state, and presentation meet only in `main.py`.

This mirrors Alex's tripp-teleop pattern: "Pure presentation — no I/O, no serial, no recording logic."

### 6.1 Layout

```
┌─ Bus ────────────────────────────────────────────────────────┐
│ CAN2 1Mbps   state: ACTIVE   TEC 0  REC 0  ESR 0x0000        │
│ TX: SYNC 1234  POLL 1234  INIT 12  mb_busy 0                 │
│ RX: valid 4920  unknown 0  short 0  overrun 0                │
│ Cycles: period 10ms  slip 0  max_jitter 23µs                 │
├─ Nodes ──────────────────────────────────────────────────────┤
│ ID  State    RX/s  RX_tot  Miss%  Lat   Ang(°)  Vel    Fl   │
│  1  ONLINE   100   12345   0.0    180   45.2   +0.05  V     │
│  2  ONLINE   100   12345   0.0    240   90.1   -0.02  V     │
│  3  MISSING    0       0  100.0     -      -      -    -    │
│  4  ONLINE    98   12300   2.0    410  135.4  +1.20   V,S   │
├─ Events (newest at bottom) ──────────────────────────────────┤
│ 12100ms  WARN  BUS    TEC 0→4                                │
│ 8912ms   WARN  NODE   Node 4: 2 misses                       │
│ 4523ms   INFO  NODE   Node 4 first reply                     │
├─ Status ─────────────────────────────────────────────────────┤
│ /dev/ttyACM0  level=2  rx=4923 lines  parse_err=0  paused=N  │
└──────────────────────────────────────────────────────────────┘
[0-3] verbosity   [c] clear events   [r] reset counters   [p] pause   [q] quit
```

### 6.2 Keybinds

| Key | Action | Wire |
|---|---|---|
| `0`-`3` | set verbosity | `SET level=N` |
| `c` | clear event log | local + `RESET events` |
| `r` | reset counters | `RESET counters` |
| `p` | pause UI refresh | local-only |
| `q` | quit | close serial |

### 6.3 Port auto-detect

`serial.tools.list_ports.comports()` filtered by USB VID `0x16C0` (PJRC/Teensy).

- exactly 1 match → use silently
- 0 matches → print clear error and exit
- >1 matches → list them and require `--port` flag

---

## 7. File-level impact summary

### New firmware files (`teensy_master/`)
| File | Purpose | LOC budget |
|---|---|---|
| `debug_log.h` | severity enum, event struct, ring API, line-emitter macros | ~40 |
| `debug_log.cpp` | ring buffer, formatters, throttling | ~150 |
| `node_state.h` | NodeState enum, NodeTelemetry struct, transition API | ~40 |
| `node_state.cpp` | transition function, state-event emission | ~80 |
| `host_control.h` | command API | ~25 |
| `host_control.cpp` | line buffer, parser, ACK emitter | ~100 |

### Edited firmware files
| File | Change | Net LOC |
|---|---|---|
| `teensy_master.ino` | replace ad-hoc globals with telemetry struct; integrate node SM; instrument TX/RX; periodic INIT retry | +60 / -25 |
| `config.h` | add `DEBUG_LEVEL`, `EVENT_RING_SIZE`, `NODE_MISS_TO_MISSING`, `NODE_STABLE_CYCLES` | +6 |

### New Python files (`Textual_python/`)
| File | Purpose | LOC budget |
|---|---|---|
| `protocol.py` | parse_line, cmd_set_level, cmd_reset_*, cmd_ping | ~80 |
| `state.py` | dataclasses + derived @property | ~120 |
| `serial_reader.py` | thread, auto-detect, TX/RX | ~120 |
| `widgets.py` | BusPanel, NodesPanel, EventsPanel, StatusBar | ~150 |
| `main.py` | App, BINDINGS, action_* handlers | ~80 |
| `yam.tcss` | colors, layout | ~40 |
| `README.md` | run instructions | — |

Total: ~415 LOC firmware, ~590 LOC Python. All firmware files within CLAUDE.md ≤200 LOC guidance.

---

# PART 2 — IMPLEMENTATION HANDOFF

This part is structured for a fresh Claude session. Each step is self-contained.

## Pre-flight checks

Before touching code:

```bash
# 1. Confirm shared lib symlink exists
ls -l ~/Arduino/libraries/yam_can_protocol

# 2. Confirm sketches compile in current state
# (open Arduino IDE 2.x and verify both teensy_master and ra4m1_node compile)

# 3. Confirm working tree clean
cd /home/oreste/Arduino/Verlet_robotics
git status
```

Required reads before implementing:
- `Yam_passive_RA4M1/DOCS/Architecture.md` (system overview)
- `Yam_passive_RA4M1/DOCS/Error_handling.md` (this file)
- `Yam_passive_RA4M1/teensy_master/teensy_master.ino`
- `Yam_passive_RA4M1/teensy_master/config.h`
- `Yam_passive_RA4M1/shared/can_protocol.h`
- `Yam_passive_RA4M1/ra4m1_node/ra4m1_node.ino` (no edits in this pass; for context only)

**Do not modify** `ra4m1_node/` or `shared/can_protocol.h` in this pass.

---

## Implementation order

Build firmware first (steps 1-7), flash, verify with `cat /dev/ttyACM0`. Then build Python (steps 8-13). Each firmware step compiles standalone.

### Step 1 — `teensy_master/config.h` additions

Append to existing file:

```cpp
//=============================================================================
// Debug / Telemetry
//=============================================================================
#define DEBUG_LEVEL_DEFAULT       2     // 0=silent  1=events  2=summary  3=trace
#define EVENT_RING_SIZE           16    // ring buffer capacity (lines)
#define NODE_STABLE_CYCLES        10    // DISCOVERED -> ONLINE after N consecutive RX
#define NODE_MISS_TO_MISSING      3     // ONLINE -> MISSING after N misses
#define NODE_MISS_TO_OFFLINE      100   // MISSING -> OFFLINE (use existing COMM_FAULT_THRESHOLD)
#define DEBUG_HOST_BAUD           115200
#define HOST_CMD_LINE_MAX         64    // max command line length in bytes
```

Keep existing `DEBUG_ENABLED` and `DEBUG_PRINT_PERIOD_MS` for backward compatibility.

---

### Step 2 — `teensy_master/debug_log.h`

```cpp
#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <Arduino.h>
#include <stdint.h>

enum class Severity : uint8_t { INFO = 0, WARN = 1, ERR = 2, FATAL = 3 };
enum class EvtClass : uint8_t { BUS, TX, NODE, FRAME, TIMING, SYSTEM };

void dbg_init();
void dbg_set_level(uint8_t lvl);   // 0..3
uint8_t dbg_get_level();

// Emit STAT/NODE lines — caller builds the data; emitter just writes if level >= 2.
void dbg_emit_stat_line(const char* line);
void dbg_emit_node_line(const char* line);

// Emit one EVT line and store in ring. Severity >= ERR always prints regardless of level.
void dbg_event(Severity sev, EvtClass cls, uint8_t node, const char* fmt, ...);

// Reset counters / events
void dbg_reset_events();

// Throttle helper: returns true at most once per `period_ms` per unique tag.
// `tag` should be a string literal so its pointer is stable.
bool dbg_throttle_ok(const char* tag, uint32_t period_ms);

#endif
```

---

### Step 3 — `teensy_master/debug_log.cpp`

Key elements:

- Static ring buffer `EventEntry events[EVENT_RING_SIZE]` with head index + count.
- `dbg_event(...)`: snprintf into local buffer (size 128), store in ring with timestamp, then emit immediately if `level >= 1` OR severity >= ERR.
- Output format string:
  ```
  EVT  t=%lu sev=%s cls=%s node=%u msg="%s"\n
  ```
  Use `Serial.print` not `Serial.printf` if portability matters; Teensy supports printf.
- `dbg_throttle_ok`: maintain a small static array of `(tag_ptr, last_emit_ms)` pairs. Used in main.ino for things like "TEC stepped" to avoid spam.

Severity-to-string and class-to-string lookup tables:
```cpp
static const char* SEV_STR[]  = { "INFO ", "WARN ", "ERR  ", "FATAL" };
static const char* CLS_STR[]  = { "BUS",   "TX",    "NODE",  "FRAME", "TIMING", "SYSTEM" };
```

Boot-time level is initialized from `DEBUG_LEVEL_DEFAULT` in config.h; `dbg_set_level` overrides at runtime.

---

### Step 4 — `teensy_master/node_state.h`

```cpp
#ifndef NODE_STATE_H
#define NODE_STATE_H

#include <stdint.h>

enum class NodeState : uint8_t {
    UNSEEN     = 0,
    DISCOVERED = 1,
    ONLINE     = 2,
    MISSING    = 3,
    OFFLINE    = 4
};

const char* nodeStateName(NodeState s);

struct NodeTelemetry {
    NodeState state;
    uint32_t  first_seen_ms;
    uint32_t  rx_count;            // lifetime
    uint32_t  rx_in_window;        // resets each 1 Hz tick
    uint32_t  last_rx_us;
    uint32_t  last_latency_us;
    uint16_t  consecutive_rx;      // for DISCOVERED -> ONLINE
    uint16_t  consecutive_miss;    // for ONLINE -> MISSING -> OFFLINE
    uint8_t   init_retries_seen;   // INIT TXs from master before first reply
    // Current sample
    float     angle_rad;
    float     velocity_rad_s;
    uint8_t   status;
};

// Call when a SAMPLE_REPLY arrives.
// `cycle_init_retries` = total INIT TXs since boot (for [INFO] event detail).
void nodeOnReceive(NodeTelemetry& nt, uint8_t node_id,
                   float angle, float vel, uint8_t status,
                   uint32_t now_ms, uint32_t latency_us,
                   uint32_t cycle_init_retries);

// Call once per poll cycle for nodes that did NOT reply this cycle.
void nodeOnMiss(NodeTelemetry& nt, uint8_t node_id, uint32_t now_ms);

#endif
```

---

### Step 5 — `teensy_master/node_state.cpp`

`nodeOnReceive` transitions:
- `UNSEEN → DISCOVERED`: set `first_seen_ms`, emit `INFO/NODE` event `"first reply at t=NNms after K INIT retries"`.
- `DISCOVERED → ONLINE`: when `consecutive_rx >= NODE_STABLE_CYCLES`, emit `INFO/NODE` event `"stable (ONLINE)"`.
- `MISSING → ONLINE` (recovery): when `consecutive_rx >= NODE_STABLE_CYCLES`, emit `INFO/NODE` event `"recovered"`.
- `OFFLINE → DISCOVERED` (recurrence): emit `INFO/NODE` event `"reappeared"`.

Always update `rx_count++`, `rx_in_window++`, reset `consecutive_miss = 0`, increment `consecutive_rx`, update angle/vel/status/last_rx_us/last_latency_us.

`nodeOnMiss` transitions:
- Increment `consecutive_miss`, reset `consecutive_rx = 0`.
- `ONLINE → MISSING` at `consecutive_miss >= NODE_MISS_TO_MISSING`: emit `WARN/NODE` event `"N consecutive misses"`.
- `MISSING → OFFLINE` at `consecutive_miss >= NODE_MISS_TO_OFFLINE`: emit `ERR/NODE` event `"OFFLINE — N consecutive misses"`.

`nodeStateName`: returns `"UNSEEN" / "DISC" / "ONLINE" / "MISSING" / "OFFLINE"` (note: `DISCOVERED` printed as `DISC` to fit fixed-width columns).

---

### Step 6 — `teensy_master/host_control.h`

```cpp
#ifndef HOST_CONTROL_H
#define HOST_CONTROL_H

#include <stdint.h>

// Initialize internal line buffer.
void host_ctrl_init();

// Call from loop() — drains Serial.available(), parses any complete lines.
void host_ctrl_poll();

#endif
```

---

### Step 7 — `teensy_master/host_control.cpp`

Static line buffer `char line[HOST_CMD_LINE_MAX]`; index. On each `host_ctrl_poll()`:
1. While `Serial.available()`, read one byte.
2. If `\n` or `\r`: terminate line, dispatch, reset index.
3. Else if `idx < HOST_CMD_LINE_MAX - 1`: append.
4. Else (overflow): emit NAK, reset.

Dispatch (case-sensitive uppercase tokens; tolerate trailing whitespace):
- `SET level=N` (N=0..3) → `dbg_set_level(N)`, print `ACK level=N`
- `RESET counters` → clear all telemetry counters, print `ACK reset=counters`
- `RESET events` → `dbg_reset_events()`, print `ACK reset=events`
- `PING` → print `PONG t=<millis>`
- `DUMP config` → print one `CFG key=value` line per knob from config.h
- anything else → `NAK cmd="..." reason="parse error"`

Use `sscanf` or hand-rolled tokenization. Keep it dependency-free.

---

### Step 8 — `teensy_master/teensy_master.ino` edits

Anchor by existing section comments. Apply in order:

**A. Add includes** (after existing `#include <can_protocol.h>`):
```cpp
#include "debug_log.h"
#include "node_state.h"
#include "host_control.h"
```

**B. Replace** the existing `NodeSample` struct + `g_nodes[NUM_NODES]` array with:
```cpp
static NodeTelemetry g_nodes[NUM_NODES];
```

**C. Add telemetry counters** (replace existing scattered globals):
```cpp
struct MasterTelemetry {
    uint32_t sync_tx, poll_tx, init_tx;
    uint32_t tx_mb_busy, tx_aborts;
    uint32_t rx_valid, rx_unknown_id, rx_short_dlc, rx_mb_ovr;
    uint32_t cycle_count, cycle_slip;
    uint32_t cycle_jitter_max_us;
    uint32_t last_cycle_us;
    uint8_t  last_tec, last_rec;
    uint16_t last_esr1;
    uint8_t  bus_state;   // 0=ACTIVE 1=WARNING 2=PASSIVE 3=BUS_OFF
};
static MasterTelemetry g_tel = {};
static uint32_t g_last_init_retry_ms = 0;
static uint32_t g_last_summary_ms = 0;
```

**D. Edit each `txSync` / `txPollSample` / `txInit`** to:
- increment the corresponding counter on success
- increment `g_tel.tx_mb_busy` on `can2.write()` returning false (replaces `tx_errors`).

**E. Replace `discoverNodes()`** — keep an initial 3-burst INIT for fast startup, but route every received SAMPLE_REPLY through `nodeOnReceive(...)`. The state machine handles discovery transitions; no separate `g_active_nodes` bitmask is needed.

**F. Edit `loop()`**:

1. At the top of the 100 Hz cycle, **periodic INIT retry**:
   ```cpp
   bool any_unseen = false;
   for (uint8_t i = 0; i < NUM_NODES; i++) {
       if (g_nodes[i].state == NodeState::UNSEEN || g_nodes[i].state == NodeState::OFFLINE) {
           any_unseen = true; break;
       }
   }
   if (any_unseen && (now_ms - g_last_init_retry_ms >= INIT_RETRY_MS)) {
       g_last_init_retry_ms = now_ms;
       txInit();
   }
   ```

2. Capture `uint32_t sync_tx_us = micros();` immediately before `txSync()`. Use this for per-node latency in the RX block.

3. In the RX classifier, expand cases:
   ```cpp
   if (msg.id >= CAN_ID_SAMPLE_BASE + 1 && msg.id <= CAN_ID_SAMPLE_BASE + NUM_NODES) {
       uint8_t node_idx = msg.id - CAN_ID_SAMPLE_BASE - 1;
       if (msg.len < FRAME_SIZE_SAMPLE_REPLY - 1) {  // tolerate seq_num absent
           g_tel.rx_short_dlc++;
           if (dbg_throttle_ok("rx_short", 1000))
               dbg_event(Severity::WARN, EvtClass::FRAME, msg.id - CAN_ID_SAMPLE_BASE,
                         "short DLC on ID 0x%03X (len=%u, expected %u)",
                         msg.id, msg.len, FRAME_SIZE_SAMPLE_REPLY);
           continue;
       }
       // decode angle/vel/status as before
       uint32_t latency_us = micros() - sync_tx_us;
       nodeOnReceive(g_nodes[node_idx], node_idx + 1,
                     decodeAngle(angle_raw), decodeVelocity(vel_raw), status,
                     now_ms, latency_us, g_tel.init_tx);
       g_tel.rx_valid++;
   } else {
       g_tel.rx_unknown_id++;
       if (dbg_throttle_ok("rx_unk", 1000))
           dbg_event(Severity::WARN, EvtClass::FRAME, 0,
                     "unknown ID 0x%03X", msg.id);
   }
   ```

4. **After the RX window**, for every node not RX'd this cycle, call `nodeOnMiss(g_nodes[i], i + 1, now_ms)`.

5. **CAN error counter polling**: after RX window, read FlexCAN counters via FlexCAN_T4 helpers (TEC/REC). On change, throttled WARN/ERR events for BUS class. State transitions (ACTIVE↔PASSIVE↔BUS_OFF) emit ERR/FATAL.

6. **Cycle slip detect**: if `(now_ms - last_poll_ms) > POLL_PERIOD_MS * 2`, increment `g_tel.cycle_slip` and emit WARN/TIMING (throttled).

7. **1 Hz summary block**: replace the existing `if (now_ms - last_debug >= DEBUG_PRINT_PERIOD_MS)` block with one that:
   - Calls `dbg_emit_stat_line(buf)` with the formatted STAT line.
   - Calls `dbg_emit_node_line(buf)` once per node (loop 1..NUM_NODES, always 7 lines).
   - Resets `g_nodes[i].rx_in_window = 0` after emission.
   - `dbg_emit_*` internally honors the verbosity gate.

**G. In `setup()`**:
```cpp
dbg_init();
host_ctrl_init();
dbg_set_level(DEBUG_LEVEL_DEFAULT);
// ... existing CAN init and discovery burst ...
dbg_event(Severity::INFO, EvtClass::SYSTEM, 0, "master boot: nodes=%u baud=%lu", NUM_NODES, CAN_BAUD);
```

**H. In `loop()`** — first line:
```cpp
host_ctrl_poll();
```

---

### Step 9 — `Textual_python/protocol.py`

```python
"""Wire format definitions and pure parser. No I/O, no presentation."""
from dataclasses import dataclass
from typing import Optional

# Severity values match firmware enum
SEV_INFO, SEV_WARN, SEV_ERR, SEV_FATAL = 0, 1, 2, 3
SEV_FROM_STR = {"INFO": 0, "WARN": 1, "ERR": 2, "FATAL": 3}

CLASSES = ("BUS", "TX", "NODE", "FRAME", "TIMING", "SYSTEM")

@dataclass
class ParsedLine:
    kind: str            # "STAT" / "NODE" / "EVT" / "ACK" / "NAK" / "PONG" / "CFG" / "UNKNOWN"
    fields: dict         # key -> value (str)
    raw: str

def parse_line(line: str) -> ParsedLine:
    """Parse one wire line into kind+fields. Robust to extra whitespace."""
    s = line.strip()
    if not s:
        return ParsedLine("UNKNOWN", {}, line)
    # First token is kind, rest are key=value pairs (with possible quoted strings for msg=)
    tokens = _tokenize(s)
    if not tokens:
        return ParsedLine("UNKNOWN", {}, line)
    kind = tokens[0].upper()
    fields = {}
    for tok in tokens[1:]:
        if "=" in tok:
            k, _, v = tok.partition("=")
            fields[k] = v.strip('"')
    return ParsedLine(kind, fields, line)

def _tokenize(s: str) -> list[str]:
    """Split on whitespace, respecting double-quoted strings."""
    out, buf, in_q = [], [], False
    for ch in s:
        if ch == '"':
            in_q = not in_q
            buf.append(ch)
        elif ch.isspace() and not in_q:
            if buf:
                out.append("".join(buf)); buf = []
        else:
            buf.append(ch)
    if buf:
        out.append("".join(buf))
    return out

# Command builders
def cmd_set_level(n: int) -> bytes:
    return f"SET level={int(n)}\n".encode("ascii")
def cmd_reset_counters() -> bytes:
    return b"RESET counters\n"
def cmd_reset_events() -> bytes:
    return b"RESET events\n"
def cmd_ping() -> bytes:
    return b"PING\n"
def cmd_dump_config() -> bytes:
    return b"DUMP config\n"
```

---

### Step 10 — `Textual_python/state.py`

```python
"""Pure state dataclasses. No I/O, no Textual imports."""
from dataclasses import dataclass, field
from enum import IntEnum

class NodeState(IntEnum):
    UNSEEN, DISCOVERED, ONLINE, MISSING, OFFLINE = 0, 1, 2, 3, 4

STATE_FROM_STR = {
    "UNSEEN": NodeState.UNSEEN, "DISC": NodeState.DISCOVERED,
    "ONLINE": NodeState.ONLINE, "MISSING": NodeState.MISSING,
    "OFFLINE": NodeState.OFFLINE,
}
STATE_NAME = {v: k for k, v in STATE_FROM_STR.items()}
STATE_STYLE = {
    NodeState.UNSEEN:     "dim white",
    NodeState.DISCOVERED: "cyan",
    NodeState.ONLINE:     "green",
    NodeState.MISSING:    "yellow",
    NodeState.OFFLINE:    "bold red",
}

SEV_NAME  = {0: "INFO", 1: "WARN", 2: "ERR ", 3: "FATAL"}
SEV_STYLE = {0: "cyan", 1: "yellow", 2: "bold red", 3: "bold red on white"}

@dataclass
class BusStats:
    tec: int = 0
    rec: int = 0
    esr1: int = 0
    state: str = "UNKNOWN"
    sync_tx: int = 0
    poll_tx: int = 0
    init_tx: int = 0
    mb_busy: int = 0
    rx_valid: int = 0
    rx_unknown: int = 0
    rx_short: int = 0
    rx_overrun: int = 0
    cycle_slip: int = 0
    max_jitter_us: int = 0
    last_t_ms: int = 0
    poll_count: int = 0

    @property
    def health(self) -> str:
        if self.state == "BUS_OFF": return "FAIL"
        if self.state in ("PASSIVE", "WARNING") or self.rx_overrun > 0: return "WARN"
        if self.tec > 0 or self.rec > 0: return "WARN"
        return "GOOD"

@dataclass
class NodeRow:
    node_id: int
    state: NodeState = NodeState.UNSEEN
    rx_per_sec: int = 0
    rx_total: int = 0
    miss_count: int = 0
    last_latency_us: int = 0
    angle_deg: float = 0.0
    velocity: float = 0.0
    flags: str = "-"

    @property
    def miss_pct(self) -> float:
        seen = self.rx_total + self.miss_count
        return 100.0 * self.miss_count / max(1, seen)

@dataclass
class EventEntry:
    t_ms: int
    severity: int
    cls: str
    msg: str
    node: int = 0

@dataclass
class ConnectionStats:
    port: str = "?"
    rx_lines: int = 0
    parse_errors: int = 0
    serial_errors: int = 0
    last_rx_ms: float = 0.0
    connected: bool = False

@dataclass
class LiveState:
    bus: BusStats = field(default_factory=BusStats)
    nodes: dict[int, NodeRow] = field(default_factory=dict)
    events: list[EventEntry] = field(default_factory=list)
    conn: ConnectionStats = field(default_factory=ConnectionStats)
    paused: bool = False
    current_level: int = 2

    def push_event(self, ev: EventEntry, max_events: int = 200):
        self.events.append(ev)
        if len(self.events) > max_events:
            self.events.pop(0)
```

Apply parsed lines to state in `main.py` (or a small dispatcher in `state.py` if it stays pure-data — keep dispatch in main).

---

### Step 11 — `Textual_python/serial_reader.py`

```python
"""Threaded serial I/O. Pure I/O — no presentation."""
import threading, time
from typing import Callable, Optional
import serial
from serial.tools import list_ports

TEENSY_VID = 0x16C0  # PJRC

def find_teensy_port(explicit: Optional[str] = None) -> str:
    if explicit:
        return explicit
    matches = [p for p in list_ports.comports() if p.vid == TEENSY_VID]
    if len(matches) == 1:
        return matches[0].device
    if not matches:
        raise SystemExit("No Teensy found (VID 0x16C0). Pass --port /dev/ttyACMx.")
    listing = "\n  ".join(p.device for p in matches)
    raise SystemExit(f"Multiple Teensies found:\n  {listing}\nPass --port to pick one.")

class SerialReader:
    """Background thread that reads lines and pushes to a callback."""
    def __init__(self, port: str, baud: int,
                 on_line: Callable[[str], None],
                 on_error: Callable[[str], None]):
        self.port_name = port
        self.baud = baud
        self.on_line = on_line
        self.on_error = on_error
        self._ser: Optional[serial.Serial] = None
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()

    def start(self):
        try:
            self._ser = serial.Serial(self.port_name, self.baud, timeout=0.1)
        except serial.SerialException as e:
            self.on_error(f"open {self.port_name}: {e}")
            return
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def stop(self):
        self._stop.set()
        if self._ser and self._ser.is_open:
            self._ser.close()

    def send(self, payload: bytes):
        try:
            if self._ser and self._ser.is_open:
                self._ser.write(payload)
        except serial.SerialException as e:
            self.on_error(f"write: {e}")

    def _loop(self):
        assert self._ser is not None
        buf = b""
        while not self._stop.is_set():
            try:
                chunk = self._ser.read(256)
                if chunk:
                    buf += chunk
                    while b"\n" in buf:
                        line, _, buf = buf.partition(b"\n")
                        try:
                            text = line.decode("ascii", errors="replace").rstrip("\r")
                        except Exception:
                            text = ""
                        if text:
                            self.on_line(text)
            except serial.SerialException as e:
                self.on_error(f"read: {e}")
                time.sleep(0.5)
```

---

### Step 12 — `Textual_python/widgets.py`

One `Static` subclass per panel, with `update_state(...)` that re-renders using Rich. Pattern matches Alex's `build_display(state)` — pure presentation, no I/O.

```python
"""Pure presentation. Consumes state, renders Rich content. No I/O."""
from textual.widgets import Static
from rich.table import Table
from rich.text import Text
from rich.panel import Panel
from rich import box

from state import (BusStats, NodeRow, EventEntry, ConnectionStats,
                   STATE_NAME, STATE_STYLE, SEV_NAME, SEV_STYLE)

class BusPanel(Static):
    def update_state(self, b: BusStats):
        t = Table.grid(padding=(0, 2))
        t.add_column(); t.add_column()
        state_style = ("green" if b.state == "ACTIVE"
                       else "yellow" if b.state in ("WARNING", "PASSIVE")
                       else "bold red" if b.state == "BUS_OFF"
                       else "dim")
        t.add_row("Bus state", Text(b.state, style=state_style))
        t.add_row("TEC / REC", f"{b.tec} / {b.rec}")
        t.add_row("ESR1", f"0x{b.esr1:04X}")
        t.add_row("TX SYNC/POLL/INIT", f"{b.sync_tx} / {b.poll_tx} / {b.init_tx}")
        t.add_row("TX mb_busy", str(b.mb_busy))
        t.add_row("RX v/u/s/ovr", f"{b.rx_valid} / {b.rx_unknown} / {b.rx_short} / {b.rx_overrun}")
        t.add_row("Cycle slip / jitter", f"{b.cycle_slip} / {b.max_jitter_us}µs")
        self.update(Panel(t, title="Bus", border_style="blue"))

class NodesPanel(Static):
    def update_state(self, nodes: dict[int, NodeRow]):
        t = Table(box=box.SIMPLE_HEAD, expand=True)
        for col in ("ID", "State", "RX/s", "RX_total", "Miss%", "Lat", "Ang(°)", "Vel", "Fl"):
            t.add_column(col)
        for nid in range(1, 8):  # always show all 7 rows
            n = nodes.get(nid)
            if n is None:
                t.add_row(str(nid), Text("UNSEEN", style="dim"),
                          "0", "0", "-", "-", "-", "-", "-")
                continue
            state_text = Text(STATE_NAME[n.state], style=STATE_STYLE[n.state])
            t.add_row(
                str(n.node_id), state_text,
                str(n.rx_per_sec), str(n.rx_total),
                f"{n.miss_pct:.1f}",
                f"{n.last_latency_us}" if n.last_latency_us else "-",
                f"{n.angle_deg:.1f}" if n.state != NodeState.UNSEEN else "-",
                f"{n.velocity:+.2f}" if n.state != NodeState.UNSEEN else "-",
                n.flags,
            )
        self.update(Panel(t, title="Nodes", border_style="blue"))

class EventsPanel(Static):
    MAX_VISIBLE = 20
    def update_state(self, events: list[EventEntry]):
        t = Table.grid(padding=(0, 1))
        for _ in range(4): t.add_column()
        recent = events[-self.MAX_VISIBLE:]
        for ev in recent:
            sev = Text(SEV_NAME[ev.severity], style=SEV_STYLE[ev.severity])
            t.add_row(f"{ev.t_ms}ms", sev, ev.cls, ev.msg)
        self.update(Panel(t, title="Events", border_style="blue"))

class StatusBar(Static):
    def update_state(self, c: ConnectionStats, level: int, paused: bool):
        line = Text()
        line.append(c.port, style="bold" if c.connected else "bold red")
        line.append(f"   level={level}", style="cyan")
        line.append(f"   rx={c.rx_lines}", style="dim")
        line.append(f"   parse_err={c.parse_errors}",
                    style="red" if c.parse_errors else "dim")
        if paused:
            line.append("   [PAUSED]", style="bold yellow")
        self.update(line)
```

Note: import `NodeState` in widgets.py — fix the truncated import line above.

---

### Step 13 — `Textual_python/main.py`

```python
"""Textual app. Wires reader + state + widgets + bindings."""
import argparse
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Vertical
from textual.widgets import Header, Footer

from state import LiveState, BusStats, NodeRow, EventEntry, NodeState, STATE_FROM_STR
from protocol import (parse_line, cmd_set_level, cmd_reset_counters,
                      cmd_reset_events, cmd_ping, SEV_FROM_STR)
from serial_reader import SerialReader, find_teensy_port
from widgets import BusPanel, NodesPanel, EventsPanel, StatusBar

class YamDebugApp(App):
    CSS_PATH = "yam.tcss"
    BINDINGS = [
        Binding("q", "quit", "Quit"),
        Binding("0", "set_level('0')", "Silent"),
        Binding("1", "set_level('1')", "Events"),
        Binding("2", "set_level('2')", "Summary"),
        Binding("3", "set_level('3')", "Trace"),
        Binding("c", "clear_events", "Clear"),
        Binding("r", "reset_counters", "Reset"),
        Binding("p", "toggle_pause", "Pause"),
    ]

    def __init__(self, port: str, baud: int = 115200):
        super().__init__()
        self.state = LiveState()
        self.state.conn.port = port
        self.reader = SerialReader(port, baud,
                                   on_line=self._on_line,
                                   on_error=self._on_serial_err)

    def compose(self) -> ComposeResult:
        yield Header()
        with Vertical():
            yield BusPanel(id="bus")
            yield NodesPanel(id="nodes")
            yield EventsPanel(id="events")
        yield StatusBar(id="status")
        yield Footer()

    def on_mount(self) -> None:
        self.reader.start()
        self.state.conn.connected = True
        self.set_interval(0.1, self._refresh)

    def _refresh(self) -> None:
        if self.state.paused:
            return
        self.query_one("#bus", BusPanel).update_state(self.state.bus)
        self.query_one("#nodes", NodesPanel).update_state(self.state.nodes)
        self.query_one("#events", EventsPanel).update_state(self.state.events)
        self.query_one("#status", StatusBar).update_state(
            self.state.conn, self.state.current_level, self.state.paused)

    # --- ingestion ---------------------------------------------------------
    def _on_line(self, line: str) -> None:
        self.state.conn.rx_lines += 1
        p = parse_line(line)
        try:
            if p.kind == "STAT":   self._apply_stat(p.fields)
            elif p.kind == "NODE": self._apply_node(p.fields)
            elif p.kind == "EVT":  self._apply_evt(p.fields)
            elif p.kind in ("ACK", "PONG", "NAK", "CFG"): pass
            else:
                self.state.conn.parse_errors += 1
        except Exception:
            self.state.conn.parse_errors += 1

    def _on_serial_err(self, msg: str) -> None:
        self.state.conn.serial_errors += 1
        self.state.conn.connected = False

    def _apply_stat(self, f: dict) -> None:
        b = self.state.bus
        b.last_t_ms = int(f.get("t", b.last_t_ms))
        b.poll_count = int(f.get("poll", b.poll_count))
        b.tec = int(f.get("tec", b.tec))
        b.rec = int(f.get("rec", b.rec))
        b.esr1 = int(f.get("esr", "0"), 16)
        b.state = f.get("bus", b.state)
        b.sync_tx = int(f.get("sync", b.sync_tx))
        b.poll_tx = int(f.get("polltx", b.poll_tx))
        b.init_tx = int(f.get("init", b.init_tx))
        b.mb_busy = int(f.get("mbb", b.mb_busy))
        b.rx_valid = int(f.get("rxv", b.rx_valid))
        b.rx_unknown = int(f.get("rxu", b.rx_unknown))
        b.rx_short = int(f.get("rxs", b.rx_short))
        b.rx_overrun = int(f.get("ovr", b.rx_overrun))
        b.cycle_slip = int(f.get("slip", b.cycle_slip))
        b.max_jitter_us = int(f.get("jit", b.max_jitter_us))

    def _apply_node(self, f: dict) -> None:
        nid = int(f.get("id", "0"))
        if nid == 0: return
        n = self.state.nodes.setdefault(nid, NodeRow(node_id=nid))
        n.state = STATE_FROM_STR.get(f.get("st", "UNSEEN"), NodeState.UNSEEN)
        n.rx_per_sec = int(f.get("rxs", "0"))
        n.rx_total = int(f.get("rxt", "0"))
        n.miss_count = int(f.get("miss", "0"))
        lat = f.get("lat", "-")
        n.last_latency_us = int(lat) if lat != "-" else 0
        ang = f.get("ang", "-")
        n.angle_deg = float(ang) if ang != "-" else 0.0
        vel = f.get("vel", "-")
        n.velocity = float(vel) if vel != "-" else 0.0
        n.flags = f.get("fl", "-")

    def _apply_evt(self, f: dict) -> None:
        ev = EventEntry(
            t_ms=int(f.get("t", "0")),
            severity=SEV_FROM_STR.get(f.get("sev", "INFO").strip(), 0),
            cls=f.get("cls", "?"),
            msg=f.get("msg", ""),
            node=int(f.get("node", "0")),
        )
        self.state.push_event(ev)

    # --- actions -----------------------------------------------------------
    def action_set_level(self, lvl: str) -> None:
        self.state.current_level = int(lvl)
        self.reader.send(cmd_set_level(int(lvl)))
    def action_clear_events(self) -> None:
        self.state.events.clear()
        self.reader.send(cmd_reset_events())
    def action_reset_counters(self) -> None:
        self.reader.send(cmd_reset_counters())
    def action_toggle_pause(self) -> None:
        self.state.paused = not self.state.paused

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", help="Serial port (auto-detected if omitted)")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()
    port = find_teensy_port(args.port)
    YamDebugApp(port, args.baud).run()

if __name__ == "__main__":
    main()
```

---

### Step 14 — `Textual_python/yam.tcss`

```css
Screen { background: $surface; }
Header { dock: top; }
Footer { dock: bottom; }
#bus    { height: 11; }
#nodes  { height: 13; }
#events { height: 1fr; }
#status { dock: bottom; height: 1; background: $boost; color: $text; }
```

---

### Step 15 — `Textual_python/README.md`

Briefly: install deps, run command, keybinds reference.

```
pip install pyserial textual rich
python main.py            # auto-detects Teensy
python main.py --port /dev/ttyACM0

Keybinds:
  0-3  set verbosity (silent/events/summary/trace)
  c    clear event log
  r    reset master counters
  p    pause UI refresh
  q    quit
```

---

## Validation checklist

Run after each major step.

### After firmware steps 1-8
- [ ] `teensy_master/` compiles in Arduino IDE
- [ ] Flash to Teensy 4.1
- [ ] `cat /dev/ttyACM0` shows lines matching: `^(STAT|NODE|EVT|ACK|PONG|CFG|NAK) `
- [ ] Within 2 seconds of boot, see 1 `STAT` + 7 `NODE` lines per second
- [ ] With 4 nodes powered, see 4 `EVT t=... sev=INFO cls=NODE msg="first reply..."` lines (one per node)
- [ ] If a node is unpowered at boot, then powered later: `EVT` reports `first reply after K INIT retries` with K > 0
- [ ] `echo "SET level=1" > /dev/ttyACM0` → `ACK level=1` and STAT/NODE lines stop; EVT lines continue
- [ ] `echo "SET level=2" > /dev/ttyACM0` → ACK and full output resumes
- [ ] `echo "PING" > /dev/ttyACM0` → `PONG t=NNNN`

### After Python steps 9-15
- [ ] `python main.py` launches with auto-detected port
- [ ] BusPanel populates within 2 seconds
- [ ] NodesPanel shows all 7 rows (4 ONLINE, 3 UNSEEN/MISSING)
- [ ] EventsPanel scrolls with INFO events on node discovery
- [ ] Press `0` → master goes silent within 1 second; status bar shows `level=0`
- [ ] Press `2` → output resumes
- [ ] Press `r` → counters in BusPanel return to 0; resume climbing
- [ ] Unplug a node mid-run → NODE row turns yellow (MISSING) within 30 ms, red (OFFLINE) after 1 s
- [ ] Plug it back → row returns to ONLINE with recovery event in EventsPanel

### Stress test
- [ ] Run for 10 minutes with all 4 nodes connected; verify zero parse_errors in StatusBar
- [ ] Toggle level 0 ↔ 3 repeatedly; verify no missed commands and no crashes

---

## What this pass does NOT do

Explicitly out of scope; flag for follow-up TODOs:

- Node-side `CAN.write()` return-value check (suspect C1 on RA4M1)
- Stale-data detection via `seq_num` (requires master to parse `buf[7]`)
- M5 host binary stream coexistence on USB Serial
- M8+ fault state machine on nodes
- Node-side diagnostic reply (DIAG_REPLY frames, M9)
- Persistent log to file (`--log` flag)

These are independent fixes that can layer on top once the telemetry backbone is in place.

---

## Handoff notes

After completing all steps:

1. Commit firmware and Python changes in separate commits with messages explaining the *why*, not the *what*.
2. Update `DOCS/Architecture.md` §5.3 Module Structure to list the new files.
3. Add a "Known Failures" entry to project context if any test in the checklist failed.
4. The wire format (§5 above) is the contract between firmware and Python — any future change to either side must update §5 first.

---

_End of document._
