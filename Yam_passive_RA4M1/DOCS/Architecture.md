# Yam_passive_RA4M1 — CAN Encoder Network

## Document Status
- **Version:** 1.0
- **Date:** 2026-04-30
- **Status:** Implementation Plan

---

## 1. System Overview

7-node absolute encoder network for the YAM passive robotic arm using CAN bus communication. Each joint has an MT6701 magnetic encoder read by a dedicated XIAO RA4M1 microcontroller. A Teensy 4.1 master broadcasts SYNC frames for synchronized sampling and polls all nodes at 100 Hz.

### Architecture Diagram

```
                              Host PC
                                │
                           USB Serial
                                │
                       ┌────────┴────────┐
                       │   Teensy 4.1    │
                       │     Master      │
                       │                 │
                       │  CAN2 @ 1 Mbps  │
                       │  FlexCAN_T4     │
                       └────────┬────────┘
                                │
                    ┌───────────┴───────────┐
                    │   CAN Bus (shared)    │
                    │   TJA1051 @ each end  │
                    │   120Ω termination    │
                    └───────────┬───────────┘
                                │
          ┌──────┬──────┬──────┼──────┬──────┬──────┐
          │      │      │      │      │      │      │
        Node1  Node2  Node3  Node4  Node5  Node6  Node7
        RA4M1  RA4M1  RA4M1  RA4M1  RA4M1  RA4M1  RA4M1
          │      │      │      │      │      │      │
        MT6701 MT6701 MT6701 MT6701 MT6701 MT6701 MT6701
       (base)                                    (end effector)
```

### Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Bus type | Standard CAN (not FD) | RA4M1 doesn't support CAN FD |
| Sampling | SYNC frame triggered | Minimal skew between nodes (~1 µs) |
| Payload | 16-bit precision | Fits 8-byte CAN frame; exceeds MT6701 14-bit resolution |
| Timestamp | Master-side at SYNC TX | Single clock source, no sync complexity |
| Velocity | Node-side computation | Better resolution, 500 Hz internal sampling |
| Acceleration | Reserved (future) | Bytes allocated, filled with 0 |
| Node ID | Compile-time constant | Simple; same as Yam_passive UART version |
| Fault handling | Phased implementation | Basic first, production-level later |

---

## 2. Hardware Specifications

### Master: Teensy 4.1

| Parameter | Value |
|-----------|-------|
| MCU | IMXRT1062 (ARM Cortex-M7 @ 600 MHz) |
| CAN peripheral | CAN2 via FlexCAN_T4 |
| CAN TX pin | 1 |
| CAN RX pin | 0 |
| Host interface | USB Serial |
| CAN transceiver | TJA1051 (CAN Pal) |

### Node: Seeed XIAO RA4M1

| Parameter | Value |
|-----------|-------|
| MCU | Renesas RA4M1 (ARM Cortex-M4 @ 48 MHz) |
| Flash | 256 KB |
| RAM | 32 KB |
| CAN peripheral | CAN0 (native, standard CAN only) |
| CAN TX pin | D18 (P103) — requires modified pins_arduino.h |
| CAN RX pin | D17 (P102) — requires modified pins_arduino.h |
| SPI peripheral | RSPI for MT6701 |
| CAN transceiver | TJA1051 (CAN Pal) |

### Encoder: MT6701

| Parameter | Value |
|-----------|-------|
| Resolution | 14-bit (16384 counts/rev) |
| Interface | SSI (3-wire SPI, read-only) |
| Frame size | 24 bits |
| SPI mode | MODE1 (CPOL=0, CPHA=1) |
| SPI clock | 1 MHz |
| CRC | 6-bit polynomial 0x03 |

### Pin Assignments

**XIAO RA4M1 Node:**

| Function | Pin | Notes |
|----------|-----|-------|
| MT6701 SCK | D8 | SPI clock |
| MT6701 MISO | D9 | SPI data in |
| MT6701 CS | D5 | Chip select |
| CAN TX | D18 | Modified variant |
| CAN RX | D17 | Modified variant |
| LED | LED_BUILTIN | Heartbeat/status |

**Teensy 4.1 Master:**

| Function | Pin | Notes |
|----------|-----|-------|
| CAN2 TX | 1 | FlexCAN_T4 default |
| CAN2 RX | 0 | FlexCAN_T4 default |
| Host | USB | Serial monitor / host_receiver.py |

---

## 3. CAN Protocol Specification

### 3.1 CAN ID Assignment

IDs are assigned by priority (lower = higher priority).

| ID | Name | Direction | DLC | Purpose |
|----|------|-----------|-----|---------|
| 0x000 | SYNC | Master → All | 0 | Trigger synchronized sampling |
| 0x010 | INIT | Master → All | 1 | Enable nodes to respond |
| 0x0F1-0x0F7 | FAULT_EVENT | Node → Master | 8 | Unsolicited fault alert |
| 0x100 | POLL_SAMPLE | Master → All | 0 | Request sample from all nodes |
| 0x1F1-0x1F7 | POLL_DIAG | Master → Node | 0 | Request diagnostics |
| 0x201-0x207 | SAMPLE_REPLY | Node → Master | 8 | Encoder data |
| 0x2F1-0x2F7 | DIAG_REPLY | Node → Master | 8 | Diagnostic data |

Node ID is encoded in the CAN ID: `base_id + node_id`

### 3.2 Frame Formats

#### SYNC Frame
```
CAN ID:  0x000
DLC:     0
Payload: (none)
Purpose: All nodes sample MT6701 immediately on RX
```

#### INIT Frame
```
CAN ID:  0x010
DLC:     1
Payload:
  [0] flags (uint8)
      bit 0: enable_responses (1 = nodes may respond to POLL)
```

#### POLL_SAMPLE Frame
```
CAN ID:  0x100
DLC:     0
Payload: (none)
Purpose: All enabled nodes reply with SAMPLE_REPLY
```

#### SAMPLE_REPLY Frame
```
CAN ID:  0x200 + node_id
DLC:     8
Payload:
  [0:1]  angle_raw (uint16, little-endian)
         Encoding: 0-65535 maps to 0-2π radians
         Resolution: 0.0055° (360° / 65536)
  [2:3]  velocity_raw (int16, little-endian)
         Encoding: ±32767 maps to ±100 rad/s
         Resolution: 0.003 rad/s
  [4:5]  accel_raw (int16, little-endian)
         Reserved for future use, fill with 0
  [6]    status_flags (uint8)
         See Status Flags table below
  [7]    seq_num (uint8)
         Monotonic counter, wraps at 255
```

#### POLL_DIAG Frame
```
CAN ID:  0x1F0 + node_id
DLC:     0
Payload: (none)
```

#### DIAG_REPLY Frame
```
CAN ID:  0x2F0 + node_id
DLC:     8
Payload:
  [0]    seq_num (uint8)
  [1]    fault_state (uint8)
         0 = INIT, 1 = HEALTHY, 2 = DEGRADED, 3 = FAULTED
  [2:3]  consecutive_fail (uint16, little-endian)
  [4:5]  total_errors (uint16, little-endian)
  [6]    status_flags (uint8)
  [7]    can_health (uint8)
         0 = healthy, 255 = bus-off
```

#### FAULT_EVENT Frame
```
CAN ID:  0x0F0 + node_id
DLC:     8
Payload: Same as DIAG_REPLY
Rate limit: Max 1 per 100 ms per node
Trigger: fault_state transitions to FAULTED or DEGRADED
```

### 3.3 Status Flags

| Bit | Mask | Flag | Meaning |
|-----|------|------|---------|
| 0 | 0x01 | STATUS_VALID | Sample data is valid |
| 1 | 0x02 | STATUS_STALE | Data is stale (frozen) |
| 2 | 0x04 | STATUS_SENSOR_FAULT | Sensor fault threshold exceeded |
| 3 | 0x08 | STATUS_FIELD_WEAK | MT6701 magnetic field weak |
| 4 | 0x10 | STATUS_FIELD_STRONG | MT6701 magnetic field strong |
| 5 | 0x20 | STATUS_CRC_ERROR | Recent SPI CRC error |
| 6 | 0x40 | STATUS_DEGRADED | Node in DEGRADED state |
| 7 | 0x80 | STATUS_RECOVERING | Node recovering from fault |

### 3.4 Encoding/Decoding

```cpp
// Angle: uint16 → radians
float angle_rad = (float)angle_raw * (TWO_PI / 65536.0f);

// Radians → uint16
uint16_t angle_raw = (uint16_t)(angle_rad * (65536.0f / TWO_PI));

// Velocity: int16 → rad/s (±100 rad/s range)
float velocity_rad_s = (float)velocity_raw * (100.0f / 32767.0f);

// rad/s → int16
int16_t velocity_raw = (int16_t)(velocity_rad_s * (32767.0f / 100.0f));
```

---

## 4. Timing Architecture

### 4.1 Master Poll Cycle (100 Hz)

```
Time (ms)  0    1    2    3    4    5    6    7    8    9    10
           |----|----|----|----|----|----|----|----|----|----|

SYNC TX    [S]
           │
           └──► All nodes sample MT6701 (~30 µs)

POLL TX         [P]
                │
                └──► Nodes TX SAMPLE_REPLY

RX Window            [R1][R2][R3][R4][R5][R6][R7]
                     └─ ~100 µs per node ─┘

Host TX                                              [H]
                                                     └─ Stream to PC
```

### 4.2 Timing Budget

| Stage | Duration | Notes |
|-------|----------|-------|
| SYNC TX | 50 µs | 0-byte frame at 1 Mbps |
| Node sample | 30 µs | SPI read + filter |
| POLL TX | 50 µs | 0-byte frame |
| SAMPLE_REPLY (×7) | 700 µs | 8-byte frame × 7 nodes |
| Master processing | 100 µs | Parse + aggregate |
| Host TX | 200 µs | USB serial |
| **Total** | **~1.2 ms** | Budget: 10 ms ✓ |

### 4.3 Node Internal Timing

```
500 Hz Timer ISR:
  └─ Set sample_due flag (if not SYNC-triggered)

Main Loop:
  if (sync_received) {
    sample_mt6701();
    sync_received = false;
  }
  if (poll_received && enabled) {
    tx_sample_reply();
    poll_received = false;
  }
```

---

## 5. Software Architecture

### 5.1 Node State Machine

```
                    ┌──────────┐
           boot ───►│   INIT   │
                    └────┬─────┘
                         │ MT6701 OK
                         ▼
                    ┌──────────┐
                    │WAIT_INIT │◄─────────────────────┐
                    └────┬─────┘                      │
                         │ INIT frame received        │
                         ▼                            │
                    ┌──────────┐                      │
         ┌─────────►│ HEALTHY  │                      │
         │          └────┬─────┘                      │
         │               │ consecutive_fail >= 10    │
         │               ▼                            │
         │          ┌──────────┐                      │
         │          │ DEGRADED │                      │
         │          └────┬─────┘                      │
         │               │ consecutive_fail >= 500   │
         │               ▼                            │
         │          ┌──────────┐                      │
         │          │ FAULTED  │──── power cycle ─────┘
         │          └────┬─────┘
         │               │ consecutive_good >= 50
         │               ▼
         │          ┌───────────┐
         └──────────│RECOVERING │
   consecutive_good └───────────┘
        >= 100
```

### 5.2 Master State Machine

```
                    ┌──────────┐
           boot ───►│   INIT   │
                    └────┬─────┘
                         │ CAN OK
                         ▼
                    ┌──────────┐
                    │ DISCOVER │──── TX INIT, wait for nodes
                    └────┬─────┘
                         │ timeout or nodes present
                         ▼
                    ┌──────────┐
                    │ RUNNING  │◄─────────────────────┐
                    └────┬─────┘                      │
                         │ all nodes faulted         │
                         ▼                            │
                    ┌──────────┐                      │
                    │  ERROR   │──── nodes recover ───┘
                    └──────────┘
```

### 5.3 Module Structure

**Node (ra4m1_node/):**

| File | Responsibility |
|------|----------------|
| `ra4m1_node.ino` | Main loop, state machine |
| `config.h` | NODE_ID, pins, timing constants |
| `mt6701.h/.cpp` | SPI driver, CRC6, stuck-line detection |
| `can_node.h/.cpp` | CAN TX/RX, frame builders |

**Master (teensy_master/):**

| File | Responsibility |
|------|----------------|
| `teensy_master.ino` | Main loop, scheduler |
| `config.h` | CAN IDs, timing, node list |
| `system_state.h/.cpp` | NodeState array, accessors |
| `can_master.h/.cpp` | CAN TX/RX, frame parsers |
| `host_stream.h/.cpp` | Host frame builder, USB TX |

**Shared (shared/):**

| File | Responsibility |
|------|----------------|
| `can_protocol.h` | CAN IDs, frame sizes, encoding macros, status flags |
| `library.properties` | Arduino library manifest |

---

## 6. Fault Handling

### 6.1 Phase 1: Basic Fault Detection (Milestone 8)

| Fault | Detection | Response |
|-------|-----------|----------|
| SPI CRC error | CRC6 mismatch | Increment counter, set STATUS_CRC_ERROR |
| SPI stuck line | All 0xFF or 0x00 | Treat as CRC error |
| Sensor fault | 500 consecutive fails | Set STATUS_SENSOR_FAULT, freeze output |
| Comm timeout | No reply in 2 ms | Increment miss counter |
| Comm fault | 100 consecutive misses | Set comm_faulted, freeze output |

### 6.2 Phase 2: Production Fault Handling (Milestone 12, Future)

| Enhancement | Description |
|-------------|-------------|
| Hysteresis | Require 50 good samples before clearing FAULTED |
| State machine | Formal HEALTHY → DEGRADED → FAULTED → RECOVERING |
| Event alerts | FAULT_EVENT frame on state transition |
| Rate limiting | Max 1 event per 100 ms per node |
| Watchdog | RA4M1 WDT resets on main loop hang |
| Bus health | Monitor CAN TEC/REC counters |

---

## 7. Host Interface

### 7.1 Host Frame Format (USB Serial)

```
[0]      0xCC (FRAME_START_HOST)
[1:4]    timestamp_us (uint32, little-endian)
[5]      n_nodes (uint8)
[6:6+10×N] node_data (repeated):
  [+0]     node_id (uint8)
  [+1:+4]  angle (float32, little-endian, radians)
  [+5:+8]  velocity (float32, little-endian, rad/s)
  [+9]     status (uint8)
[6+10×N] crc8
```

### 7.2 Host Alert Frame

```
[0]      0xCD (FRAME_START_ALERT)
[1]      node_id (uint8)
[2]      fault_type (uint8)
[3]      fault_state (uint8)
[4:7]    timestamp_us (uint32, little-endian)
[8]      crc8
```

---

## 8. Implementation Milestones

### Milestone 0: Project Scaffolding
**Goal:** Directory structure, config files, empty sketches that compile

**Deliverables:**
- Project directory structure created
- Empty sketches with setup/loop
- Config files with constants
- mt6701 driver copied and adapted
- shared/can_protocol.h with definitions

**Checkpoint:**
- [ ] Both sketches compile with no errors
- [ ] Symlink shared/ to Arduino libraries
- [ ] Flash both boards, see "Setup complete" on Serial

---

### Milestone 1: Single Node MT6701 Read
**Goal:** One RA4M1 reads MT6701 at 500 Hz, prints to Serial

**Implement:**
- Timer-triggered sampling (500 Hz)
- SPI read with CRC6 validation
- EMA filter on angle
- Velocity calculation
- Serial debug output

**Checkpoint:**
- [ ] Angle changes when magnet rotates
- [ ] Velocity sign correct for CW/CCW
- [ ] No CRC errors with stable magnet
- [ ] 500 Hz rate verified (timestamp deltas ~2000 µs)

**Validation:** Rotate magnet, observe angle/velocity on Serial Monitor

---

### Milestone 2: Master CAN TX (SYNC + POLL)
**Goal:** Teensy transmits SYNC and POLL_SAMPLE at 100 Hz

**Implement:**
- FlexCAN_T4 setup on CAN2
- 100 Hz timer for poll cycle
- TX SYNC frame (ID 0x000)
- TX POLL_SAMPLE frame (ID 0x100)
- Serial debug showing TX activity

**Checkpoint:**
- [ ] CAN frames visible on scope/analyzer
- [ ] 100 Hz rate verified
- [ ] CAN error counters remain 0

**Validation:** Scope on CAN_H/CAN_L shows frame bursts

---

### Milestone 3: Node CAN RX (SYNC triggers sample)
**Goal:** Node receives SYNC, samples MT6701, stores result

**Implement:**
- Arduino_CAN setup
- RX filter for SYNC (0x000)
- SYNC RX triggers immediate MT6701 read
- Store sample in buffer
- Serial debug: "SYNC RX" + sample data

**Checkpoint:**
- [ ] Node prints "SYNC RX" at 100 Hz
- [ ] Sample timestamp jitter < 100 µs
- [ ] MT6701 read completes before POLL arrives

**Validation:** Connect to master CAN bus, observe prints

---

### Milestone 4: Node CAN TX (SAMPLE_REPLY)
**Goal:** Node responds to POLL_SAMPLE with encoder data

**Implement:**
- RX filter for POLL_SAMPLE (0x100)
- Build SAMPLE_REPLY frame (8 bytes)
- TX on POLL_SAMPLE RX
- Sequence number increment

**Checkpoint:**
- [ ] Master sees SAMPLE_REPLY frames
- [ ] Angle value matches magnet position
- [ ] seq_num increments each frame
- [ ] No dropped frames over 10 seconds

**Validation:** Master prints received angle

---

### Milestone 5: Master CAN RX + Aggregation
**Goal:** Master receives replies, aggregates, streams to host

**Implement:**
- RX handler for SAMPLE_REPLY (0x201-0x207)
- NodeState array
- Timeout detection (2 ms)
- Host frame builder
- 100 Hz USB stream

**Checkpoint:**
- [ ] Master decodes angle correctly
- [ ] Timeout detected if node unplugged
- [ ] Host frame received on PC

**Validation:** Python script decodes stream

---

### Milestone 6: Multi-Node Operation
**Goal:** 2+ nodes on bus, all responding

**Implement:**
- Flash second node with NODE_ID=2
- Master handles multiple reply IDs

**Checkpoint:**
- [ ] Both nodes respond to SYNC/POLL
- [ ] No CAN collisions
- [ ] Host stream shows both nodes
- [ ] Latency < 2 ms total

**Validation:** Python shows 2 nodes at 100 Hz

---

### Milestone 7: INIT Command + Startup
**Goal:** Nodes wait for INIT before responding

**Implement:**
- Node state: WAIT_INIT → RUNNING
- Master TX INIT on startup
- Node ignores POLL until INIT received
- Master discovery phase

**Checkpoint:**
- [ ] Node silent before INIT
- [ ] Node responds after INIT
- [ ] Master reports node presence
- [ ] Power cycle recovers correctly

**Validation:** Power cycle, system resumes

---

### Milestone 8: Basic Fault Detection
**Goal:** Detect and report faults

**Implement:**
- Node: consecutive_fail counter, STATUS flags
- Node: freeze output on fault
- Master: timeout counter, comm_faulted
- Host stream includes fault flags

**Checkpoint:**
- [ ] Disconnect MT6701 → SENSOR_FAULT
- [ ] Disconnect node → COMM_FAULT
- [ ] Reconnect → faults clear
- [ ] Host shows fault flags

**Validation:** Physical disconnect test

---

### Milestone 9: Diagnostic Frames
**Goal:** Periodic diagnostics + events

**Implement:**
- Master: POLL_DIAG at 1 Hz (rotating)
- Node: DIAG_REPLY
- Node: FAULT_EVENT on transition

**Checkpoint:**
- [ ] Diagnostics every 7 sec per node
- [ ] FAULT_EVENT within 100 ms
- [ ] No duplicate events

**Validation:** Trigger fault, observe event + periodic

---

### Milestone 10: Host Alerting
**Goal:** Immediate fault notification

**Implement:**
- Master: alert frame on fault
- Host: parse alerts separately

**Checkpoint:**
- [ ] Alert within 10 ms of fault
- [ ] Alert includes node ID + type
- [ ] Normal stream continues

**Validation:** Python shows alert on fault

---

### Milestone 11: Full 7-Node Integration
**Goal:** All 7 nodes, production timing

**Implement:**
- Flash all 7 nodes
- Verify timing budget
- Stress test

**Checkpoint:**
- [ ] All 7 nodes at 100 Hz
- [ ] Zero dropped frames over 1 hour
- [ ] Latency < 3 ms total
- [ ] CAN errors remain 0

**Validation:** 1-hour soak test

---

### Milestone 12: Production Fault Handling (Future)
**Goal:** Hysteresis, state machine, graceful degradation

**Deferred items:**
- Formal fault state machine
- Hysteresis on recovery
- Watchdog on nodes
- Full diagnostic logging
- Configurable MIN_NODES

---

## 9. File Structure

```
Yam_passive_RA4M1/
├── ra4m1_node/
│   ├── ra4m1_node.ino
│   ├── config.h
│   ├── mt6701.h
│   ├── mt6701.cpp
│   ├── can_node.h
│   └── can_node.cpp
│
├── teensy_master/
│   ├── teensy_master.ino
│   ├── config.h
│   ├── system_state.h
│   ├── system_state.cpp
│   ├── can_master.h
│   ├── can_master.cpp
│   ├── host_stream.h
│   └── host_stream.cpp
│
├── shared/
│   ├── can_protocol.h
│   └── library.properties
│
├── host_receiver/
│   └── host_receiver.py
│
└── DOCS/
    ├── Architecture.md
    └── Hardware_Instructions.md
```

---

## 10. Dependencies

| Component | Library | Source |
|-----------|---------|--------|
| Teensy CAN | FlexCAN_T4 | Included with Teensyduino |
| RA4M1 CAN | Arduino_CAN | Seeed board package |
| Host receiver | pyserial | `pip install pyserial` |

---

## 11. References

- [Yam_passive UART version](../Yam_passive/) — Original architecture
- [Can_RA4M1_test](../Can_RA4M1_test/) — CAN prototype and debug notes
- [RA4M1_MT6701](../RA4M1_MT6701/) — SPI diagnostic for RA4M1
- [Can_debug.md](../Can_RA4M1_test/Can_debug.md) — Arduino_CAN library bugs

---

_End of document._
