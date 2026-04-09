# Master-Node Architecture: MT6701 Encoder Network

## Document Status
- **Version:** 1.1
- **Date:** 2026-04-05
- **Status:** Implemented

---

# Implementation Status

## Completed (2026-04-05)

### Phase 1: Node Firmware ✅
| Step | Status | Notes |
|------|--------|-------|
| 1.1 Project Setup | ✅ | Using `seed_node/` folder |
| 1.2 Core Sampling | ✅ | 500 Hz timer, EMA filter, velocity calc |
| 1.3 UART Protocol | ✅ | CRC8, all frame types implemented |
| 1.4 Fault Management | ✅ | Consecutive fail tracking, stale flags |

### Phase 2: Master Firmware ✅
| Step | Status | Notes |
|------|--------|-------|
| 2.1 Project Setup | ✅ | Using `teensy_master/` folder |
| 2.2 serial_interface | ✅ | MultiUartBus, UartHostStream |
| 2.3 system_state | ✅ | NodeState, SystemState structs |
| 2.4 fault_diagnostics | ✅ | Timeout, miss counting, freeze logic |
| 2.5 scheduler | ✅ | Cooperative scheduler (no ISR needed) |
| 2.6 node_manager | ✅ | Poll sequencing, reply parsing, host stream |
| 2.7 Main Integration | ✅ | All tasks running at correct rates |

### Phase 3: Integration Testing ✅
| Test | Status | Result |
|------|--------|--------|
| 3.1 Single Node | ✅ | GET_SAMPLE, GET_DIAG, PING working |
| 3.2 Two Node Operation | ✅ | 100 Hz polling, 100% success rate |
| 3.3 Fault Injection | ✅ | CRC6 + stuck-line detection added |
| 3.4 Comm Fault | ⏳ | Ready to test |

### Additional Components ✅
| Component | Status | Notes |
|-----------|--------|-------|
| Host Receiver (Python) | ✅ | `host_receiver/host_receiver.py` |
| MT6701 CRC6 Verification | ✅ | Polynomial 0x03, stuck-line detection |
| Shared Protocol Library | ✅ | Symlinked as Arduino library |

## Verified Metrics (2 nodes)
- **Poll success rate:** 100% (10974/10974 in test run)
- **Round-trip latency:** ~2-4 ms per node
- **Host stream:** 100 Hz to PC via FTDI
- **Node uptime:** Stable over 3+ minutes

## Implementation Deviations
| Spec | Actual | Reason |
|------|--------|--------|
| SPI Mode | MODE0 (not MODE1) | MODE0 works with MT6701 hardware |
| Folder names | `seed_node/`, `teensy_master/` | Existing project structure |
| scheduler.h | Not separate module | Cooperative loop sufficient for V1 |

---

# 1. System Requirements

## 1.1 Functional Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-1 | Master polls each node at 100 Hz | Must |
| FR-2 | Nodes sample MT6701 locally at 500 Hz | Must |
| FR-3 | Master streams aggregated data to host at 100 Hz | Must |
| FR-4 | Nodes report: angle (float32 rad), velocity (float32 rad/s), status flags | Must |
| FR-5 | Nodes report diagnostics at 1 Hz (lower priority) | Should |
| FR-6 | On sensor failure, node reports stale-but-flagged data | Must |
| FR-7 | Master freezes output state on faulted/stale input | Must |
| FR-8 | System supports 2 nodes initially, extensible to 6 | Must |

## 1.2 Non-Functional Requirements

| ID | Requirement | Value |
|----|-------------|-------|
| NFR-1 | Node-side sensor fault threshold | 500 consecutive bad reads |
| NFR-2 | Master-side missing threshold | 100 consecutive missed polls |
| NFR-3 | Fault persistence | RAM-only (v1) |
| NFR-4 | Production startup | All configured nodes must be present |
| NFR-5 | Latency budget (poll → reply) | < 2 ms |
| NFR-6 | Jitter tolerance | < 500 us |

## 1.3 Hardware Specifications

### Master: Teensy 4.1
| Parameter | Value |
|-----------|-------|
| MCU | IMXRT1062 (ARM Cortex-M7 @ 600 MHz) |
| Node 1 UART | Serial1 (TX=1, RX=0) |
| Node 2 UART | Serial2 (TX=8, RX=7) |
| Host Stream | Serial8 (TX=35, RX=34) → FTDI → PC |
| Baud Rate | 115200 (all ports) |

### Node: Seeed XIAO RP2040
| Parameter | Value |
|-----------|-------|
| MCU | RP2040 (Dual Cortex-M0+ @ 133 MHz) |
| Master UART | UART0: TX=D6 (GPIO0), RX=D7 (GPIO1) |
| MT6701 SPI | SCK=D8 (GPIO2), MISO=D9 (GPIO4), CS=D5 (GPIO7) |
| SPI Clock | 1 MHz |
| SPI Mode | MODE0 (CPOL=0, CPHA=0) - verified working |

### Encoder: MT6701
| Parameter | Value |
|-----------|-------|
| Resolution | 14-bit (16384 counts/rev) |
| Interface | SSI (3-wire SPI, read-only) |
| Frame Size | 24 bits |
| Angle LSB | 360/16384 = 0.02197 deg |

---

# 2. System Architecture

## 2.1 Physical Topology (V1: Point-to-Point UART)

```
                          +------------------+
                          |    HOST PC       |
                          |  (Dashboard/Log) |
                          +--------+---------+
                                   |
                            FTDI USB-Serial
                            115200 baud
                                   |
                          +--------+---------+
                          |   TEENSY 4.1     |
                          |     MASTER       |
                          |                  |
                          | Serial8 (Host)   |
                          | Serial1 (Node 1) |
                          | Serial2 (Node 2) |
                          +--+----------+----+
                             |          |
              +--------------+          +--------------+
              |                                        |
        Serial1                                  Serial2
        115200                                   115200
              |                                        |
     +--------+--------+                      +--------+--------+
     |  XIAO RP2040    |                      |  XIAO RP2040    |
     |    NODE 1       |                      |    NODE 2       |
     |   (ID = 1)      |                      |   (ID = 2)      |
     +--------+--------+                      +--------+--------+
              |                                        |
           SPI @ 1MHz                              SPI @ 1MHz
              |                                        |
     +--------+--------+                      +--------+--------+
     |     MT6701      |                      |     MT6701      |
     |    ENCODER      |                      |    ENCODER      |
     +-----------------+                      +-----------------+
```

## 2.2 Future Topology (RS485 Multidrop)

```
                          +------------------+
                          |    HOST PC       |
                          +--------+---------+
                                   |
                          +--------+---------+
                          |   TEENSY 4.1     |
                          |     MASTER       |
                          +--------+---------+
                                   |
                            RS485 Half-Duplex
                            (Single Bus)
              +--------+--------+--------+--------+--------+
              |        |        |        |        |        |
           Node 1   Node 2   Node 3   Node 4   Node 5   Node 6
```

## 2.3 Data Flow

```
+-------------+     +-------------+     +-------------+
|   MT6701    |     |   MT6701    |     |   MT6701    |
|  (500 Hz)   |     |  (500 Hz)   |     |  (500 Hz)   |
+------+------+     +------+------+     +------+------+
       |                   |                   |
       v                   v                   v
+------+------+     +------+------+     +------+------+
| Node 1      |     | Node 2      |     | Node N      |
| - Sample    |     | - Sample    |     | - Sample    |
| - Filter    |     | - Filter    |     | - Filter    |
| - Velocity  |     | - Velocity  |     | - Velocity  |
| - Reply     |     | - Reply     |     | - Reply     |
+------+------+     +------+------+     +------+------+
       |                   |                   |
       +-------------------+-------------------+
                           |
                    UART (100 Hz poll)
                           |
                    +------+------+
                    |   MASTER    |
                    | - Poll      |
                    | - Aggregate |
                    | - Fault Mgr |
                    +------+------+
                           |
                    Serial8 (100 Hz)
                           |
                    +------+------+
                    |   HOST PC   |
                    | - Dashboard |
                    | - Logging   |
                    +-------------+
```

## 2.4 Master Internal Data Flow (Module View)

```
                              SCHEDULER (1ms tick)
                                     │
           ┌─────────────────────────┼─────────────────────────┐
           │                         │                         │
           ▼                         ▼                         ▼
    ┌─────────────┐          ┌─────────────┐          ┌─────────────┐
    │bus_poll_task│          │host_tx_task │          │diag_poll    │
    │  (100 Hz)   │          │  (100 Hz)   │          │  (1 Hz)     │
    └──────┬──────┘          └──────┬──────┘          └──────┬──────┘
           │                        │                        │
           │ calls                  │ calls                  │ calls
           ▼                        ▼                        ▼
    ┌──────────────────────────────────────────────────────────────┐
    │                        NODE_MANAGER                           │
    │  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐  │
    │  │ pollNode()     │  │ sendHostFrame()│  │ pollNode()     │  │
    │  │ processReply() │  │ getNodeOutput()│  │ (GET_DIAG cmd) │  │
    │  └───────┬────────┘  └───────┬────────┘  └───────┬────────┘  │
    └──────────┼───────────────────┼───────────────────┼───────────┘
               │                   │                   │
      ┌────────┴────────┐          │          ┌───────┴────────┐
      │                 │          │          │                │
      ▼                 ▼          ▼          ▼                ▼
┌───────────┐    ┌───────────┐  ┌───────────────┐    ┌─────────────────┐
│SERIAL_    │    │SYSTEM_    │  │FAULT_         │    │shared/protocol  │
│INTERFACE  │    │STATE      │  │DIAGNOSTICS    │    │                 │
├───────────┤    ├───────────┤  ├───────────────┤    ├─────────────────┤
│INodeBus   │    │NodeState[]│  │checkTimeout() │    │FRAME_START_*    │
│ -sendReq  │    │           │  │incrementMiss()│    │CMD_*            │
│ -readReply│    │SystemState│  │shouldFreeze() │    │STATUS_*         │
│IHostStream│    │           │  │getOutput()    │    │crc8()           │
│ -sendFrame│    │           │  │               │    │                 │
└─────┬─────┘    └───────────┘  └───────────────┘    └─────────────────┘
      │
      ▼
┌─────────────────────────────────────────┐
│            HARDWARE                      │
│  Serial1 (Node 1)    Serial8 (Host PC)  │
│  Serial2 (Node 2)                       │
└─────────────────────────────────────────┘
```

---

# 3. Binary Protocol Specification

## 3.1 General Format

- **Byte Order:** Little-endian (ARM native)
- **Framing:** Start byte + fixed length + CRC8
- **CRC8 Polynomial:** 0x07 (x^8 + x^2 + x + 1), init 0x00

## 3.2 Master → Node: Request Frame (4 bytes)

```
+--------+--------+--------+--------+
| START  | NODE_ID|  CMD   |  CRC8  |
+--------+--------+--------+--------+
   0xAA    1-6      0x01+    CRC8
```

| Field | Offset | Size | Description |
|-------|--------|------|-------------|
| START | 0 | 1 | 0xAA |
| NODE_ID | 1 | 1 | Target node (1-6) |
| CMD | 2 | 1 | Command code |
| CRC8 | 3 | 1 | CRC of bytes 0-2 |

**Command Codes:**

| Code | Name | Description |
|------|------|-------------|
| 0x01 | GET_SAMPLE | Request angle + velocity |
| 0x02 | GET_DIAG | Request diagnostics |
| 0x03 | PING | Health check |
| 0x04 | ZERO | Zero current position (future) |

## 3.3 Node → Master: Sample Reply (12 bytes)

```
+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+
| START  | NODE_ID|     ANGLE (float32)       |    VELOCITY (float32)     | STATUS |  CRC8  |
+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+
   0xBB    1-6       4 bytes (LE)                  4 bytes (LE)             flags    CRC8
```

| Field | Offset | Size | Description |
|-------|--------|------|-------------|
| START | 0 | 1 | 0xBB |
| NODE_ID | 1 | 1 | Responding node (1-6) |
| ANGLE | 2 | 4 | Angle in radians (float32 LE) |
| VELOCITY | 6 | 4 | Velocity in rad/s (float32 LE) |
| STATUS | 10 | 1 | Status flags (see below) |
| CRC8 | 11 | 1 | CRC of bytes 0-10 |

**Status Flags (byte 10):**

| Bit | Name | Description |
|-----|------|-------------|
| 0 | VALID | 1 = sample valid |
| 1 | STALE | 1 = sample is stale (sensor fail) |
| 2 | SENSOR_FAULT | 1 = sensor fault threshold exceeded |
| 3 | FIELD_WEAK | 1 = MT6701 reports weak field |
| 4 | FIELD_STRONG | 1 = MT6701 reports strong field |
| 5 | CRC_ERROR | 1 = recent SPI CRC error |
| 6-7 | Reserved | 0 |

## 3.4 Node → Master: Diagnostic Reply (15 bytes)

```
+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+
| START  | NODE_ID|       UPTIME (u32)        | SENS_FAIL| UART_ERR |  RST   | CRC_CNT  |  CRC8  |
+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+
   0xBC    1-6       4 bytes (LE)                2 bytes    2 bytes    1 byte   2 bytes    CRC8
```

| Field | Offset | Size | Description |
|-------|--------|------|-------------|
| START | 0 | 1 | 0xBC |
| NODE_ID | 1 | 1 | Responding node (1-6) |
| UPTIME | 2 | 4 | Milliseconds since boot (uint32 LE) |
| SENS_FAIL | 6 | 2 | Consecutive sensor fail count (uint16 LE) |
| UART_ERR | 8 | 2 | Node-side UART error count (uint16 LE) |
| RESET_CAUSE | 10 | 1 | Reset cause code (deferred to v2) |
| CRC_CNT | 11 | 2 | Total SPI CRC errors (uint16 LE) |
| RESERVED | 13 | 1 | 0x00 |
| CRC8 | 14 | 1 | CRC of bytes 0-13 |

**Reset Cause Codes (v2 - deferred):**

| Code | Name |
|------|------|
| 0x00 | POWER_ON |
| 0x01 | WATCHDOG |
| 0x02 | SOFT_RESET |
| 0x03 | UNKNOWN |

> **Note:** Reset cause detection on RP2040 requires reading `VREG_AND_CHIP_RESET` register.
> For v1, this field will always report `UNKNOWN (0x03)`. Full implementation deferred to v2.

## 3.5 Node → Master: Ping Reply (4 bytes)

```
+--------+--------+--------+--------+
| START  | NODE_ID| STATUS |  CRC8  |
+--------+--------+--------+--------+
   0xBD    1-6      flags    CRC8
```

## 3.6 Master → Host: Stream Frame (27 bytes for 2 nodes)

```
+--------+--------+--------+--------+--------+  +--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+  +--------+
| START  |   TIMESTAMP (u32)       | N_NODES |  | NODE_ID| ANGLE  | VELOCITY | STATUS | ... repeat for each node ...                     |  CRC8   |
+--------+--------+--------+--------+--------+  +--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+  +--------+
   0xCC      4 bytes (LE)            count          1       4 bytes   4 bytes     1                                                           CRC8
```

**Frame Size Formula:** 6 + (N_NODES * 10) + 1 = 6 + 20 + 1 = 27 bytes (2 nodes)

| Field | Offset | Size | Description |
|-------|--------|------|-------------|
| START | 0 | 1 | 0xCC |
| TIMESTAMP | 1 | 4 | micros() (uint32 LE) |
| N_NODES | 5 | 1 | Number of nodes in frame |
| NODE_DATA[] | 6 | 10*N | Per-node data blocks |
| CRC8 | 6+10*N | 1 | CRC of all preceding bytes |

**Per-Node Data Block (10 bytes):**

| Field | Offset | Size | Description |
|-------|--------|------|-------------|
| NODE_ID | 0 | 1 | Node ID (1-6) |
| ANGLE | 1 | 4 | Angle (float32 LE) |
| VELOCITY | 5 | 4 | Velocity (float32 LE) |
| STATUS | 9 | 1 | Status flags |

---

# 4. Timing Architecture

## 4.1 Master Scheduler (Rate Monotonic)

```
Priority (High → Low)          Period      WCET Budget
─────────────────────────────────────────────────────
1. bus_poll_task               10 ms       4 ms    (TX + wait + RX for all nodes)
2. host_tx_task                10 ms       1 ms
3. diag_poll_task              1000 ms     2 ms
4. fault_manager_task          100 ms      0.5 ms
```

> **Note:** For V1 point-to-point UART, RX handling is synchronous within `bus_poll_task`.
> A separate `rx_worker_task` may be needed for V2 RS485 with interrupt-driven RX.

## 4.2 Master Timing Diagram (10 ms cycle)

```
Time (ms)  0    1    2    3    4    5    6    7    8    9    10
           |----|----|----|----|----|----|----|----|----|----|

bus_poll   [TX1][    ][RX1][TX2][    ][RX2][    ][    ][    ][    ]
           ^^^^        ^^^^      ^^^^
           poll N1     got N1    poll N2     got N2

host_tx    [    ][    ][    ][    ][    ][    ][    ][    ][TX ][    ]
                                                          ^^^^
                                                          stream to PC
```

## 4.3 Node Timing (500 Hz sampling)

```
Time (ms)  0    2    4    6    8    10   12   14   16   18   20
           |----|----|----|----|----|----|----|----|----|----|----|

sample     [S]  [S]  [S]  [S]  [S]  [S]  [S]  [S]  [S]  [S]  [S]
           ^    ^    ^    ^    ^    ^    ^    ^    ^    ^    ^
           every 2 ms

rx/reply        [R]            [R]            [R]
                ^              ^              ^
                when polled (async to sample)
```

## 4.4 Latency Budget

| Stage | Budget |
|-------|--------|
| Master TX request | 0.35 ms (4 bytes @ 115200) |
| Wire propagation | ~0 |
| Node RX + parse | 0.1 ms |
| Node TX reply | 1.04 ms (12 bytes @ 115200) |
| Master RX + parse | 0.2 ms |
| **Total** | **~1.7 ms** |

Margin to 10 ms period: **8.3 ms** (comfortable)

---

# 5. Software Architecture

## 5.1 Master State Machine

```
                    +-------------+
                    |    INIT     |
                    +------+------+
                           |
                    discover nodes
                           |
                           v
                    +------+------+
         +--------->|    IDLE     |<-----------+
         |          +------+------+            |
         |                 |                   |
         |          all nodes present          |
         |                 |                   |
         |                 v                   |
         |          +------+------+            |
         |          |   RUNNING   |------------+
         |          +------+------+   node lost
         |                 |          (< min required)
         |                 |
         |          fatal error
         |                 |
         |                 v
         |          +------+------+
         +----------|   FAULT     |
          recovery  +-------------+
```

## 5.2 Master Module Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              MASTER FIRMWARE                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐         │
│  │   scheduler.h   │    │  node_manager.h │    │    config.h     │         │
│  │                 │    │                 │    │                 │         │
│  │ - tick ISR      │    │ - poll sequence │    │ - pin defs      │         │
│  │ - task table    │    │ - output policy │    │ - timing        │         │
│  │ - runHighest()  │    │ - orchestration │    │ - thresholds    │         │
│  └────────┬────────┘    └────────┬────────┘    └─────────────────┘         │
│           │                      │                                          │
│           │                      │ uses                                     │
│           │                      ▼                                          │
│           │    ┌─────────────────────────────────────────────────┐         │
│           │    │              serial_interface.h                  │         │
│           │    │                                                  │         │
│           │    │  INodeBus (abstract)      IHostStream (abstract) │         │
│           │    │  - sendRequest()          - sendFrame()          │         │
│           │    │  - readReply()            - available()          │         │
│           │    │  - isReplyAvailable()                            │         │
│           │    │                                                  │         │
│           │    │  MultiUartBus (impl)      UartHostStream (impl)  │         │
│           │    │  - Serial1, Serial2...    - Serial8              │         │
│           │    └─────────────────────────────────────────────────┘         │
│           │                      │                                          │
│           │                      │ uses                                     │
│           │                      ▼                                          │
│           │    ┌─────────────────────────────────────────────────┐         │
│           │    │                system_state.h                    │         │
│           │    │                                                  │         │
│           │    │  NodeState[6]           SystemState              │         │
│           │    │  - angle, velocity      - num_configured         │         │
│           │    │  - status_flags         - num_present            │         │
│           │    │  - frozen values        - poll_index             │         │
│           │    │  - diagnostics          - frame_counter          │         │
│           │    └─────────────────────────────────────────────────┘         │
│           │                      │                                          │
│           │                      │ uses                                     │
│           │                      ▼                                          │
│           │    ┌─────────────────────────────────────────────────┐         │
│           │    │           fault_diagnostics.h                    │         │
│           │    │                                                  │         │
│           │    │  - checkTimeout(node_id, now)                    │         │
│           │    │  - incrementMissed(node_id)                      │         │
│           │    │  - clearMissed(node_id)                          │         │
│           │    │  - isFaulted(node_id)                            │         │
│           │    │  - shouldFreeze(node_id)                         │         │
│           │    │  - FAULT_THRESHOLD = 100                         │         │
│           │    └─────────────────────────────────────────────────┘         │
│           │                                                                 │
└───────────┴─────────────────────────────────────────────────────────────────┘
```

## 5.3 Master Data Structures

### system_state.h

```cpp
#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdint.h>

constexpr uint8_t MAX_NODES = 6;

// Per-node state (owned by system_state)
struct NodeState {
    uint8_t  node_id;                    // 1-6
    bool     present;                    // discovered and responding

    // Sample data (updated at 100 Hz)
    float    angle;                      // radians (latest from node)
    float    velocity;                   // rad/s (latest from node)
    uint8_t  status_flags;               // from last sample reply

    // Frozen output (used when faulted)
    float    frozen_angle;               // held on fault
    float    frozen_velocity;            // held on fault

    // Timing
    uint32_t last_rx_time_us;            // micros() of last good rx

    // Fault tracking (managed by fault_diagnostics)
    uint16_t consecutive_missed;         // missed poll counter
    bool     comm_faulted;               // true if >= 100 misses

    // Diagnostics (updated at 1 Hz via GET_DIAG)
    uint32_t uptime_ms;
    uint16_t sensor_fail_count;
    uint16_t uart_error_count;            // node-side UART errors
    uint8_t  reset_cause;                 // deferred to v2
    uint16_t crc_error_count;
};

// Global system state
struct SystemState {
    NodeState nodes[MAX_NODES];
    uint8_t   num_configured;            // how many nodes expected
    uint8_t   num_present;               // how many currently responding
    uint8_t   poll_index;                // round-robin index (0 to num_configured-1)
    uint32_t  frame_counter;             // host frames sent
    bool      running;                   // system in RUNNING state
};

// Accessors
void        systemState_init(SystemState& state, uint8_t num_nodes);
NodeState*  systemState_getNode(SystemState& state, uint8_t node_id);
void        systemState_updateFromSample(SystemState& state, uint8_t node_id,
                                          float angle, float velocity, uint8_t status);
void        systemState_updateFromDiag(SystemState& state, uint8_t node_id,
                                        uint32_t uptime, uint16_t sensor_fail,
                                        uint16_t uart_err, uint8_t reset_cause,
                                        uint16_t crc_count);

#endif // SYSTEM_STATE_H
```

### fault_diagnostics.h

```cpp
#ifndef FAULT_DIAGNOSTICS_H
#define FAULT_DIAGNOSTICS_H

#include "system_state.h"

constexpr uint16_t COMM_FAULT_THRESHOLD = 100;   // consecutive misses
constexpr uint32_t REPLY_TIMEOUT_US     = 2000;  // 2 ms timeout

// Timeout detection
bool faultDiag_checkTimeout(const NodeState& node, uint32_t now_us);

// Miss counting
void faultDiag_incrementMissed(NodeState& node);
void faultDiag_clearMissed(NodeState& node);

// Fault state
bool faultDiag_isCommFaulted(const NodeState& node);
bool faultDiag_isSensorFaulted(const NodeState& node);  // from status flags
bool faultDiag_shouldFreeze(const NodeState& node);     // comm OR sensor fault

// Freeze management
void faultDiag_freezeOutput(NodeState& node);           // copy current to frozen
void faultDiag_getOutput(const NodeState& node, float& angle, float& velocity);
                                                         // returns frozen if faulted

#endif // FAULT_DIAGNOSTICS_H
```

### serial_interface.h

```cpp
#ifndef SERIAL_INTERFACE_H
#define SERIAL_INTERFACE_H

#include <stdint.h>
#include <stddef.h>

//=============================================================================
// INodeBus - Abstract interface for node communication
//=============================================================================
class INodeBus {
public:
    virtual ~INodeBus() = default;

    // Initialize the bus
    virtual bool begin() = 0;

    // Send request frame to a specific node
    virtual void sendRequest(uint8_t node_id, const uint8_t* data, size_t len) = 0;

    // Check if reply bytes are available for a node
    virtual bool isReplyAvailable(uint8_t node_id) = 0;

    // Read reply from a node (returns bytes read, 0 on timeout)
    virtual size_t readReply(uint8_t node_id, uint8_t* buffer, size_t max_len,
                             uint32_t timeout_us) = 0;

    // Flush any pending RX data for a node
    virtual void flushRx(uint8_t node_id) = 0;
};

//=============================================================================
// IHostStream - Abstract interface for host PC communication
//=============================================================================
class IHostStream {
public:
    virtual ~IHostStream() = default;

    // Initialize the host stream
    virtual bool begin() = 0;

    // Send frame to host
    virtual void sendFrame(const uint8_t* data, size_t len) = 0;

    // Check if host has sent data (for future commands)
    virtual bool available() = 0;

    // Read from host (for future commands)
    virtual size_t read(uint8_t* buffer, size_t max_len) = 0;
};

//=============================================================================
// MultiUartBus - Point-to-point UART implementation (V1)
//=============================================================================
class MultiUartBus : public INodeBus {
public:
    // Configure which serial port maps to which node
    // node_id 1 -> ports[0], node_id 2 -> ports[1], etc.
    void configure(HardwareSerial* ports[], uint8_t num_ports, uint32_t baud);

    bool   begin() override;
    void   sendRequest(uint8_t node_id, const uint8_t* data, size_t len) override;
    bool   isReplyAvailable(uint8_t node_id) override;
    size_t readReply(uint8_t node_id, uint8_t* buffer, size_t max_len,
                     uint32_t timeout_us) override;
    void   flushRx(uint8_t node_id) override;

private:
    HardwareSerial* m_ports[6] = {nullptr};
    uint8_t         m_num_ports = 0;
    uint32_t        m_baud = 115200;
};

//=============================================================================
// UartHostStream - UART to PC implementation (V1)
//=============================================================================
class UartHostStream : public IHostStream {
public:
    void configure(HardwareSerial& port, uint32_t baud);

    bool   begin() override;
    void   sendFrame(const uint8_t* data, size_t len) override;
    bool   available() override;
    size_t read(uint8_t* buffer, size_t max_len) override;

private:
    HardwareSerial* m_port = nullptr;
    uint32_t        m_baud = 115200;
};

//=============================================================================
// Future: RS485Bus - Single bus implementation (V2)
//=============================================================================
// class RS485Bus : public INodeBus {
//     // Single HardwareSerial
//     // Direction pin control
//     // Same interface, different implementation
// };

#endif // SERIAL_INTERFACE_H
```

### node_manager.h

```cpp
#ifndef NODE_MANAGER_H
#define NODE_MANAGER_H

#include "system_state.h"
#include "serial_interface.h"
#include "fault_diagnostics.h"
#include "../shared/protocol.h"

class NodeManager {
public:
    // Dependency injection
    void init(SystemState& state, INodeBus& bus, IHostStream& host);

    //=========================================================================
    // Poll Sequencing
    //=========================================================================
    uint8_t getNextNodeId();              // returns next node to poll (1-6)
    void    advancePollIndex();           // move to next node in sequence

    //=========================================================================
    // Bus Operations (calls serial_interface)
    //=========================================================================
    void    pollNode(uint8_t node_id, uint8_t cmd);
    bool    processReply(uint8_t node_id);  // returns true if valid reply

    //=========================================================================
    // Host Stream (calls serial_interface)
    //=========================================================================
    void    sendHostFrame();              // builds and sends 100 Hz frame

    //=========================================================================
    // Output Policy
    //=========================================================================
    // Returns output values (frozen if faulted, live if healthy)
    void    getNodeOutput(uint8_t node_id, float& angle, float& velocity,
                          uint8_t& status);

private:
    SystemState*  m_state = nullptr;
    INodeBus*     m_bus   = nullptr;
    IHostStream*  m_host  = nullptr;

    // Frame buffers
    uint8_t m_tx_buffer[16];
    uint8_t m_rx_buffer[16];

    // Internal helpers
    void buildRequestFrame(uint8_t node_id, uint8_t cmd);
    bool parseReplyFrame(uint8_t node_id, size_t len);
    void buildHostFrame(uint8_t* buffer, size_t& len);
};

#endif // NODE_MANAGER_H
```

## 5.4 Node State Machine

```
                    +-------------+
                    |    INIT     |
                    +------+------+
                           |
                    init SPI, UART
                    start sample timer
                    (reset cause: v2)
                           |
                           v
                    +------+------+
         +--------->|  SAMPLING   |<-----------+
         |          +------+------+            |
         |                 |                   |
         |          rx command                 |
         |                 |                   |
         |                 v                   |
         |          +------+------+            |
         |          |   REPLY     |------------+
         |          +-------------+   tx complete
         |
         |          sensor fault
         |          (500 consecutive)
         |                 |
         |                 v
         |          +------+------+
         +----------|   FAULTED   |
          recovery  +-------------+
          (keeps sampling, flags stale)
```

## 5.5 Node Data Structures

```cpp
// Node configuration (hardcoded)
struct NodeConfig {
    uint8_t node_id;                     // 1-6, hardcoded
};

// Sample buffer (ring buffer for velocity calc)
struct SampleBuffer {
    float    angles[8];                  // last 8 samples
    uint32_t timestamps[8];              // micros() for each
    uint8_t  head;                       // write index
    uint8_t  count;                      // valid samples
};

// Node runtime state (named differently from master's NodeState to avoid confusion)
struct LocalSampleState {
    // Current sample
    float    angle_rad;                  // filtered angle
    float    velocity_rad_s;             // estimated velocity
    uint8_t  status_flags;               // sensor status

    // Fault tracking
    uint16_t consecutive_sensor_fail;    // SPI read failures
    bool     sensor_faulted;             // >= 500 failures
    float    last_valid_angle;           // for stale reporting
    float    last_valid_velocity;

    // Diagnostics
    uint32_t boot_time_ms;               // millis() at boot
    uint8_t  reset_cause;                // deferred to v2
    uint16_t total_crc_errors;           // SPI CRC6 errors (lifetime)
    uint16_t uart_error_count;           // UART framing/overrun errors

    // Timing
    volatile bool sample_due;            // set by timer ISR
};
```

---

# 6. Implementation Plan

## 6.1 Phase 1: Node Firmware (XIAO RP2040)

### Step 1.1: Project Setup
- Create `/home/alex/Arduino/Verlet_Robotics/Yam_passive/node_firmware/`
- Create `node_firmware.ino`
- Copy SPI read logic from `mt6701_spi_read.ino`

### Step 1.2: Core Sampling
- Implement 2 ms timer interrupt (500 Hz)
- Read MT6701 via SPI in main loop (flag-driven)
- Apply EMA filter (alpha = 0.4)
- Calculate velocity from filtered samples

### Step 1.3: UART Protocol
- Implement CRC8 calculation
- Implement request frame parser
- Implement sample reply builder
- Implement diagnostic reply builder

### Step 1.4: Fault Management
- Track consecutive sensor failures
- Set stale flag when failures > 0
- Set fault flag when failures >= 500
- Report last valid sample when faulted

**Deliverable:** Node responds to GET_SAMPLE, GET_DIAG, PING

## 6.2 Phase 2: Master Firmware (Teensy 4.1)

### Step 2.1: Project Setup
- Create `/home/alex/Arduino/Verlet_Robotics/Yam_passive/master_firmware/`
- Create `master_firmware.ino`
- Create `config.h` with pin definitions and timing constants

### Step 2.2: serial_interface Module
- Implement `INodeBus` abstract interface
- Implement `MultiUartBus` for Serial1/Serial2 (point-to-point)
- Implement `IHostStream` abstract interface
- Implement `UartHostStream` for Serial8

### Step 2.3: system_state Module
- Define `NodeState` and `SystemState` structs
- Implement state initialization
- Implement update functions for sample and diagnostic data

### Step 2.4: fault_diagnostics Module
- Implement timeout detection (`checkTimeout`)
- Implement miss counting (`incrementMissed`, `clearMissed`)
- Implement fault state queries (`isCommFaulted`, `shouldFreeze`)
- Implement freeze logic (`freezeOutput`, `getOutput`)

### Step 2.5: scheduler Module
- Implement 1 ms tick timer (IntervalTimer)
- Implement cooperative task scheduler
- Define task table with periods and RM priorities

### Step 2.6: node_manager Module
- Implement poll sequencing (`getNextNodeId`, `advancePollIndex`)
- Implement `pollNode` (builds frame, calls serial_interface)
- Implement `processReply` (parses frame, updates system_state)
- Implement `sendHostFrame` (builds host frame, calls serial_interface)
- Implement output policy (`getNodeOutput` with freeze logic)

### Step 2.7: Main Sketch Integration
- Instantiate `MultiUartBus`, `UartHostStream`
- Instantiate `SystemState`, `NodeManager`
- Register tasks with scheduler
- Implement task functions calling `NodeManager`

**Deliverable:** Master polls 2 nodes, streams to host with fault handling

## 6.3 Phase 3: Integration Testing

### Test 3.1: Single Node Loopback
- Connect Node 1 to Serial1
- Verify GET_SAMPLE returns valid data
- Verify GET_DIAG returns valid data
- Verify PING returns correctly

### Test 3.2: Two Node Operation
- Connect both nodes
- Verify 100 Hz polling of both
- Verify host stream contains both nodes
- Verify no crosstalk or timing issues

### Test 3.3: Fault Injection
- Disconnect MT6701 SPI wire
- Verify node reports stale flag
- Verify node reports fault after 500 failures
- Verify master freezes output

### Test 3.4: Comm Fault
- Disconnect UART wire
- Verify master increments missed polls
- Verify master marks node faulted at 100 misses
- Verify host stream shows fault status

---

# 7. Verification Checklist

## 7.1 Unit Tests (Node)

| ID | Test | Pass Criteria |
|----|------|---------------|
| N-1 | SPI read | Returns valid 14-bit angle |
| N-2 | Angle conversion | Matches expected radians |
| N-3 | EMA filter | Output smoothed correctly |
| N-4 | Velocity calc | Correct sign and magnitude |
| N-5 | CRC8 calc | Matches reference |
| N-6 | Request parse | Correctly identifies commands |
| N-7 | Reply build | Correct frame format |
| N-8 | Sample timer | 500 Hz ± 1% |
| N-9 | Fault counter | Increments on SPI fail |
| N-10 | Stale flag | Set when sensor fails |

## 7.2 Unit Tests (Master)

### scheduler
| ID | Test | Pass Criteria |
|----|------|---------------|
| M-1 | Scheduler tick | 1 ms ± 10 us |
| M-2 | Task dispatch | Correct priority order |

### serial_interface
| ID | Test | Pass Criteria |
|----|------|---------------|
| M-3 | MultiUartBus init | All ports begin at 115200 |
| M-4 | sendRequest routing | Correct port for node_id |
| M-5 | readReply timeout | Returns 0 after timeout_us |
| M-6 | UartHostStream send | Bytes written to Serial8 |

### system_state
| ID | Test | Pass Criteria |
|----|------|---------------|
| M-7 | State init | All nodes initialized correctly |
| M-8 | updateFromSample | Angle/velocity/status stored |
| M-9 | updateFromDiag | Diagnostic fields stored |

### fault_diagnostics
| ID | Test | Pass Criteria |
|----|------|---------------|
| M-10 | Timeout detect | Triggers at 2 ms |
| M-11 | Miss counter | Increments on timeout |
| M-12 | Fault threshold | Faults at 100 misses |
| M-13 | Output freeze | Returns frozen when faulted |
| M-14 | Output live | Returns live when healthy |

### node_manager
| ID | Test | Pass Criteria |
|----|------|---------------|
| M-15 | Poll sequencing | Round-robin through nodes |
| M-16 | Request build | Correct frame format |
| M-17 | Reply parse | Extracts all fields |
| M-18 | CRC8 validate | Rejects bad CRC |
| M-19 | Host frame build | Correct format with all nodes |
| M-20 | Host stream rate | 100 Hz ± 1% |

## 7.3 Integration Tests

| ID | Test | Pass Criteria |
|----|------|---------------|
| I-1 | Single node poll | < 2 ms round-trip |
| I-2 | Two node cycle | < 10 ms total |
| I-3 | Host stream rate | 100 Hz ± 1% |
| I-4 | Angle accuracy | ± 0.1 deg vs reference |
| I-5 | Velocity sign | Correct for CW/CCW |
| I-6 | Fault propagation | Host sees fault < 100 ms |
| I-7 | Recovery | Node recovers when sensor reconnected |

---

# 8. File Structure

```
/home/alex/Arduino/Verlet_Robotics/Yam_passive/
├── DOCS/
│   └── Serial_master_node_architecture.md   (this document)
│
├── shared/                                  (Arduino library: YamProtocol)
│   ├── protocol.h                           (frame definitions, CRC8, status flags)
│   ├── protocol.cpp                         (CRC8 implementation)
│   └── library.properties                   (Arduino library manifest)
│
├── seed_node/                               (XIAO RP2040 node firmware)
│   ├── seed_node.ino                        (main sketch, 500 Hz sampling)
│   ├── mt6701.h                             (SPI driver interface)
│   ├── mt6701.cpp                           (CRC6 verification, stuck-line detection)
│   └── config.h                             (node ID, pins, timing)
│
├── teensy_master/                           (Teensy 4.1 master firmware)
│   ├── teensy_master.ino                    (main sketch, cooperative scheduler)
│   ├── config.h                             (serial ports, timing, thresholds)
│   ├── system_state.h                       (NodeState, SystemState structs)
│   ├── system_state.cpp                     (state accessors)
│   ├── fault_diagnostics.h                  (timeout, miss counting, freeze logic)
│   ├── fault_diagnostics.cpp
│   ├── serial_interface.h                   (INodeBus, IHostStream interfaces)
│   ├── serial_interface.cpp                 (MultiUartBus, UartHostStream impls)
│   ├── node_manager.h                       (poll sequencing, output policy)
│   └── node_manager.cpp
│
├── host_receiver/                           (PC-side tools)
│   └── host_receiver.py                     (Python stream decoder, CSV logging)
│
├── master_test/                             (Debug/test sketches)
│   └── master_test.ino                      (Simple protocol tester)
│
└── /home/alex/Arduino/libraries/YamProtocol (symlink to shared/)
```

## 8.1 Module Responsibilities Summary

| Module | Responsibility | Touches Hardware? |
|--------|---------------|-------------------|
| `shared/protocol` | Frame formats, CRC8, status flags | No |
| `scheduler` | 1ms tick, task dispatch | Yes (timer) |
| `serial_interface` | UART TX/RX for nodes and host | Yes (Serial1-8) |
| `system_state` | NodeState[6] storage, accessors | No |
| `fault_diagnostics` | Timeout detection, miss counting, freeze | No |
| `node_manager` | Poll sequencing, reply processing, host stream | No (uses serial_interface) |
| `config` | Pin definitions, baud rates, thresholds | No |

## 8.2 Include Graph

```
master_firmware.ino
    │
    ├── config.h
    ├── scheduler.h
    └── node_manager.h
            │
            ├── system_state.h
            ├── serial_interface.h ──────► (HardwareSerial)
            ├── fault_diagnostics.h
            │       └── system_state.h
            └── ../shared/protocol.h
```

**Note:** `shared/protocol.h` is included via relative path:
```cpp
#include "../shared/protocol.h"
```

---

# 9. Pin Assignments

## 9.1 Teensy 4.1 Master

| Function | Pin | Notes |
|----------|-----|-------|
| Node 1 TX | 1 | Serial1 TX |
| Node 1 RX | 0 | Serial1 RX |
| Node 2 TX | 8 | Serial2 TX |
| Node 2 RX | 7 | Serial2 RX |
| Host TX | 35 | Serial8 TX → FTDI |
| Host RX | 34 | Serial8 RX (unused) |

## 9.2 XIAO RP2040 Node

| Function | Pin | GPIO | Notes |
|----------|-----|------|-------|
| Master TX | D6 | GPIO0 | UART0 TX |
| Master RX | D7 | GPIO1 | UART0 RX |
| MT6701 SCK | D8 | GPIO2 | SPI CLK |
| MT6701 MISO | D9 | GPIO4 | SPI Data |
| MT6701 CS | D5 | GPIO7 | SPI Chip Select |

---

# 10. Risk Register

| Risk | Impact | Mitigation |
|------|--------|------------|
| UART collision (future RS485) | Data corruption | Protocol includes CRC; defer RS485 to v2 |
| Timer drift between master/node | Polling jitter | Master is time-master; nodes are reactive |
| SPI read takes too long | Missed samples | SPI read is < 100 us; well within budget |
| Velocity noise at low speed | Jittery output | EMA filter + clamp small velocities to 0 |
| Node reset undetected | Stale data | Uptime monitored (reset cause v2); sudden uptime drop = reset |

---

# Appendix A: Shared Protocol Header (shared/protocol.h)

```cpp
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

//=============================================================================
// Frame Start Bytes
//=============================================================================
constexpr uint8_t FRAME_START_REQUEST     = 0xAA;
constexpr uint8_t FRAME_START_SAMPLE      = 0xBB;
constexpr uint8_t FRAME_START_DIAGNOSTIC  = 0xBC;
constexpr uint8_t FRAME_START_PING        = 0xBD;
constexpr uint8_t FRAME_START_HOST        = 0xCC;

//=============================================================================
// Command Codes
//=============================================================================
constexpr uint8_t CMD_GET_SAMPLE = 0x01;
constexpr uint8_t CMD_GET_DIAG   = 0x02;
constexpr uint8_t CMD_PING       = 0x03;
constexpr uint8_t CMD_ZERO       = 0x04;

//=============================================================================
// Frame Sizes
//=============================================================================
constexpr uint8_t FRAME_SIZE_REQUEST    = 4;
constexpr uint8_t FRAME_SIZE_SAMPLE     = 12;
constexpr uint8_t FRAME_SIZE_DIAGNOSTIC = 15;
constexpr uint8_t FRAME_SIZE_PING       = 4;

//=============================================================================
// Status Flags (single source of truth)
//=============================================================================
constexpr uint8_t STATUS_VALID        = 0x01;  // Bit 0: sample is valid
constexpr uint8_t STATUS_STALE        = 0x02;  // Bit 1: sample is stale
constexpr uint8_t STATUS_SENSOR_FAULT = 0x04;  // Bit 2: sensor fault threshold exceeded
constexpr uint8_t STATUS_FIELD_WEAK   = 0x08;  // Bit 3: MT6701 weak field
constexpr uint8_t STATUS_FIELD_STRONG = 0x10;  // Bit 4: MT6701 strong field
constexpr uint8_t STATUS_CRC_ERROR    = 0x20;  // Bit 5: recent SPI CRC error

// Status flag accessors (inline, zero overhead)
inline bool statusIsValid(uint8_t s)       { return s & STATUS_VALID; }
inline bool statusIsStale(uint8_t s)       { return s & STATUS_STALE; }
inline bool statusIsFaulted(uint8_t s)     { return s & STATUS_SENSOR_FAULT; }
inline bool statusIsFieldWeak(uint8_t s)   { return s & STATUS_FIELD_WEAK; }
inline bool statusIsFieldStrong(uint8_t s) { return s & STATUS_FIELD_STRONG; }
inline bool statusHasCrcError(uint8_t s)   { return s & STATUS_CRC_ERROR; }

// Status flag setters (for node-side use)
inline void statusSetValid(uint8_t& s)       { s |= STATUS_VALID; }
inline void statusSetStale(uint8_t& s)       { s |= STATUS_STALE; }
inline void statusSetFaulted(uint8_t& s)     { s |= STATUS_SENSOR_FAULT; }
inline void statusSetFieldWeak(uint8_t& s)   { s |= STATUS_FIELD_WEAK; }
inline void statusSetFieldStrong(uint8_t& s) { s |= STATUS_FIELD_STRONG; }
inline void statusSetCrcError(uint8_t& s)    { s |= STATUS_CRC_ERROR; }

inline void statusClearValid(uint8_t& s)     { s &= ~STATUS_VALID; }
inline void statusClearStale(uint8_t& s)     { s &= ~STATUS_STALE; }
inline void statusClearAll(uint8_t& s)       { s = 0; }

//=============================================================================
// Reset Cause Codes (v2 - detection deferred, always reports UNKNOWN for v1)
//=============================================================================
constexpr uint8_t RESET_POWER_ON   = 0x00;
constexpr uint8_t RESET_WATCHDOG   = 0x01;
constexpr uint8_t RESET_SOFT       = 0x02;
constexpr uint8_t RESET_UNKNOWN    = 0x03;  // default for v1

//=============================================================================
// CRC8 Function
//=============================================================================
uint8_t crc8(const uint8_t* data, size_t len);

#endif // PROTOCOL_H
```

---

# Appendix B: CRC8 Implementation (shared/protocol.cpp)

```cpp
#include "protocol.h"

// CRC8 with polynomial 0x07, init 0x00
uint8_t crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}
```

---

# Appendix C: Angle/Velocity Conversion

```cpp
// MT6701 raw to radians
constexpr float COUNTS_PER_REV = 16384.0f;
constexpr float TWO_PI = 6.28318530718f;

float rawToRadians(uint16_t raw) {
    return (float)raw * (TWO_PI / COUNTS_PER_REV);
}

// Velocity from EMA-filtered angle
// alpha = 0.4, dt = 2 ms (500 Hz)
float estimateVelocity(float new_angle, float prev_angle, float dt) {
    float delta = new_angle - prev_angle;

    // Handle wrap-around
    if (delta > 3.14159f) delta -= TWO_PI;
    if (delta < -3.14159f) delta += TWO_PI;

    return delta / dt;
}
```

---

# Appendix D: Host Receiver Usage

Python tool for receiving and decoding the 100 Hz host stream.

## Installation

```bash
pip install pyserial
```

## Usage

```bash
# Find FTDI port
ls /dev/ttyUSB*

# Basic usage
python host_receiver/host_receiver.py --port /dev/ttyUSB0

# With CSV logging
python host_receiver/host_receiver.py --port /dev/ttyUSB0 --log data.csv

# Show raw hex bytes
python host_receiver/host_receiver.py --port /dev/ttyUSB0 --raw

# All options
python host_receiver/host_receiver.py --help
```

## Output Format

```
#  1234 t= 123456789us | N1: 245.2°  +0.00r/s [VALID] | N2: 182.4°  -1.23r/s [VALID]
```

## CSV Log Format

```csv
time,timestamp_us,node_id,angle_rad,angle_deg,velocity,status
2026-04-05T20:45:00,123456789,1,4.2802,245.24,0.0,1
2026-04-05T20:45:00,123456789,2,3.1834,182.40,-1.23,1
```

---

# Appendix E: MT6701 CRC6 Verification

The MT6701 24-bit SSI frame includes a 6-bit CRC for data integrity.

## Frame Structure

```
Bit 23-10: 14-bit angle (MSB first)
Bit 9-6:   4-bit status (field[1:0], push_status, push_flag)
Bit 5-0:   6-bit CRC
```

## CRC6 Algorithm

- Polynomial: 0x03 (x^6 + x + 1)
- Init: 0x00
- Input: Upper 18 bits (angle + status)

```cpp
static uint8_t calcCRC6(uint32_t data18) {
    uint8_t crc = 0x00;
    for (int i = 17; i >= 0; i--) {
        uint8_t bit = (data18 >> i) & 0x01;
        uint8_t xor_bit = (crc >> 5) & 0x01;
        crc = (crc << 1) & 0x3F;
        if (bit ^ xor_bit) {
            crc ^= 0x03;
        }
    }
    return crc & 0x3F;
}
```

## Stuck-Line Detection

The driver also detects disconnected MISO by checking for all-zeros or all-ones:

```cpp
if ((data[0] == 0xFF && data[1] == 0xFF && data[2] == 0xFF) ||
    (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00)) {
    result.read_ok = false;  // MISO stuck
}
```