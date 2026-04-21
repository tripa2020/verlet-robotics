# YAM Passive Encoder Network — Architecture

7-node absolute encoder network for the YAM passive robotic arm. Each joint has an MT6701 magnetic encoder read by a dedicated XIAO RP2040 microcontroller. A Teensy 4.1 master polls all nodes and streams aggregated data to a host PC.

---

## System Overview

```
                         Host PC
                           │
                      FTDI USB-Serial
                           │
                    Serial8 (115200)
                           │
                  ┌────────┴────────┐
                  │   Teensy 4.1    │
                  │     Master      │
                  │                 │
                  │  100 Hz poll    │
                  │  cooperative    │
                  │  scheduler      │
                  └─┬─┬─┬─┬─┬─┬─┬──┘
                    │ │ │ │ │ │ │
          Serial1-7 │ │ │ │ │ │ │  (point-to-point UART, 115200)
                    │ │ │ │ │ │ │
               ┌────┘ │ │ │ │ │ └────┐
               │    ┌──┘ │ │ │ └──┐  │
               ▼    ▼    ▼ ▼ ▼    ▼  ▼
             Node  Node  ...     Node Node
              1     2              6    7
             XIAO  XIAO          XIAO XIAO
              │     │              │    │
             SPI   SPI            SPI  SPI
              │     │              │    │
           MT6701 MT6701        MT6701 MT6701
           (base)                    (end effector)
```

Node 1 is at the base of the arm. Node 7 is at the end effector.

---

## Data Flow

### Sensor → Node (500 Hz)

1. RP2040 repeating timer fires every 2 ms (500 Hz)
2. SPI transaction reads 24-bit frame from MT6701 (MODE1, 1 MHz)
3. Frame contains: 14-bit angle + 2-bit field status + 1-bit push + 1-bit track loss + 6-bit CRC6
4. CRC6 verified (polynomial x^6 + x + 1 over 18-bit data)
5. Raw angle converted to radians (0–2π)
6. EMA filter applied (α = 0.4) with wraparound handling at 0/2π boundary
7. Velocity calculated from filtered angle delta over time interval

### Node → Master (100 Hz, on request)

1. Master sends 4-byte request frame on the node's dedicated UART
2. Node responds with 12-byte sample frame (angle + velocity + status)
3. Master parses reply within 2 ms timeout window
4. On timeout: increment consecutive miss counter, output frozen values

### Master → Host PC (100 Hz)

1. After polling all nodes, master builds aggregated host frame
2. Frame contains timestamp + all node data (angle, velocity, status per node)
3. Sent via Serial8 → FTDI → PC USB
4. Python `host_receiver.py` decodes, displays, and optionally logs to CSV

---

## Binary Protocol

All frames use **CRC8** (polynomial 0x07, init 0x00) over all preceding bytes.

### Frame Types

| Start Byte | Name | Direction | Size |
|------------|------|-----------|------|
| `0xAA` | Request | Master → Node | 4 bytes |
| `0xBB` | Sample | Node → Master | 12 bytes |
| `0xBC` | Diagnostic | Node → Master | 15 bytes |
| `0xBD` | Ping | Node → Master | 4 bytes |
| `0xCC` | Host Aggregate | Master → PC | 6 + 10×N + 1 bytes |

### Commands

| Code | Name | Purpose |
|------|------|---------|
| `0x01` | CMD_GET_SAMPLE | Request angle + velocity |
| `0x02` | CMD_GET_DIAG | Request diagnostic counters |
| `0x03` | CMD_PING | Heartbeat check |
| `0x04` | CMD_ZERO | Zero-position offset (not yet implemented) |

### Request Frame (Master → Node) — 4 bytes

```
[0] 0xAA  (FRAME_START_REQUEST)
[1] NODE_ID
[2] COMMAND
[3] CRC8
```

### Sample Reply (Node → Master) — 12 bytes

```
[0]    0xBB  (FRAME_START_SAMPLE)
[1]    NODE_ID
[2:6]  ANGLE     (float32, little-endian, radians)
[6:10] VELOCITY  (float32, little-endian, rad/s)
[10]   STATUS_FLAGS
[11]   CRC8
```

### Diagnostic Reply (Node → Master) — 15 bytes

```
[0]     0xBC  (FRAME_START_DIAGNOSTIC)
[1]     NODE_ID
[2:6]   UPTIME_MS       (uint32, little-endian)
[6:8]   SENSOR_FAIL_COUNT (uint16, little-endian)
[8:10]  UART_ERROR_COUNT  (uint16, little-endian)
[10]    RESET_CAUSE       (0x03 = UNKNOWN for v1)
[11:13] CRC_ERROR_COUNT   (uint16, little-endian)
[13]    RESERVED
[14]    CRC8
```

### Ping Reply (Node → Master) — 4 bytes

```
[0] 0xBD  (FRAME_START_PING)
[1] NODE_ID
[2] STATUS_FLAGS
[3] CRC8
```

### Host Aggregate Frame (Master → PC) — variable length

```
[0]          0xCC  (FRAME_START_HOST)
[1:5]        TIMESTAMP_US  (uint32, little-endian)
[5]          N_NODES       (uint8)
[6:6+10×N]  NODE_DATA     (repeated per node):
    [+0]     NODE_ID
    [+1:+5]  ANGLE     (float32, little-endian)
    [+5:+9]  VELOCITY  (float32, little-endian)
    [+9]     STATUS
[6+10×N]    CRC8
```

### Status Flags (8-bit bitmask)

| Bit | Mask | Flag | Meaning |
|-----|------|------|---------|
| 0 | `0x01` | STATUS_VALID | Sensor data is valid |
| 1 | `0x02` | STATUS_STALE | Data is stale (not updated recently) |
| 2 | `0x04` | STATUS_SENSOR_FAULT | Sensor has faulted |
| 3 | `0x08` | STATUS_FIELD_WEAK | Magnetic field too weak |
| 4 | `0x10` | STATUS_FIELD_STRONG | Magnetic field too strong |
| 5 | `0x20` | STATUS_CRC_ERROR | CRC error detected |

---

## Timing and Scheduling

### Node (XIAO RP2040)

- **500 Hz sampling**: RP2040 repeating alarm sets `sample_due` flag every 2 ms
- Main loop checks flag → reads MT6701 → filters → calculates velocity
- UART responses are handled synchronously in main loop when request bytes arrive
- No ISR-based UART — all cooperative

### Master (Teensy 4.1)

Cooperative scheduler in `loop()`, no interrupts:

```
loop() {
    if (now - last_bus_poll >= 10 ms)     → busPollTask()      // 100 Hz
    if (now - last_host_tx >= 10 ms)      → hostTxTask()       // 100 Hz
    if (now - last_diag >= 1000 ms)       → diagPollTask()     // 1 Hz (one node per cycle, rotating)
    processSerialCommands()                                     // USB CLI
    if (now - last_dashboard >= 1000 ms)  → displayDashboard() // 1 Hz
}
```

### Timing Budget

| Task | Rate | Budget |
|------|------|--------|
| Node SPI read + filter | 500 Hz | < 2 ms per sample |
| Master poll round (all 7 nodes) | 100 Hz | 10 ms total, ~1.4 ms per node |
| Master → node reply timeout | — | 2 ms max wait per node |
| Host aggregate TX | 100 Hz | 10 ms period |
| Diagnostic poll | 1 Hz | One node per second, rotating through all 7 |
| Dashboard refresh | 1 Hz | Terminal ANSI display |

---

## Fault Handling

### Node-Side (Sensor Faults)

- Each SPI read is validated: CRC6 check + stuck-line detection (all 0xFF or 0x00 rejected)
- Failed reads increment a consecutive failure counter
- **500 consecutive failures** → `sensor_faulted` flag set
- While faulted: node outputs **frozen** last-valid angle and velocity with `STATUS_SENSOR_FAULT` flag
- Counter resets to 0 on any successful read → auto-recovery

### Master-Side (Communication Faults)

- Each poll that times out increments `consecutive_missed` for that node
- **100 consecutive misses** → `comm_faulted` flag set
- While faulted: master outputs **frozen** last-valid angle and velocity for that node
- Counter resets to 0 on any successful reply → auto-recovery

### Freeze Policy

Both node and master use a **freeze-not-zero** policy: when faulted, the last known good values are held rather than reporting zeros. Downstream consumers see stale-but-plausible data with the appropriate fault flag set in the status byte.

---

## Codebase Organization

```
Yam_passive/
├── seed_node/                    Node firmware (XIAO RP2040)
│   ├── seed_node.ino             Main sketch: sampling, filtering, protocol responder
│   ├── config.h                  NODE_ID, pin assignments, timing constants
│   ├── mt6701.h                  MT6701 SPI driver header
│   └── mt6701.cpp                MT6701 SPI driver: 24-bit read, CRC6, stuck-line detection
│
├── teensy_master/                Master firmware (Teensy 4.1)
│   ├── teensy_master.ino         Main sketch: cooperative scheduler, dashboard, CLI
│   ├── config.h                  7-node serial port mapping, timing, fault thresholds
│   ├── system_state.h/cpp        NodeState and SystemState structs + accessors
│   ├── node_manager.h/cpp        Poll sequencing, reply parsing, host frame building
│   ├── serial_interface.h/cpp    INodeBus / IHostStream interfaces + UART implementations
│   └── fault_diagnostics.h/cpp   Consecutive miss tracking, fault/freeze management
│
├── shared/                       YamProtocol Arduino library (symlinked to ~/Arduino/libraries/)
│   ├── protocol.h                Frame types, commands, status flags, sizes, CRC8 declaration
│   ├── protocol.cpp              CRC8 implementation (polynomial 0x07)
│   └── library.properties        Arduino library manifest
│
├── host_receiver/                Python host interface
│   └── host_receiver.py          Frame parser, CRC8 verification, display, CSV logging
│
├── master_test/                  Test utilities
│   └── master_test.ino           Single-node interactive tester (GET_SAMPLE/DIAG/PING + loop)
│
└── DOCS/                         Documentation
    ├── Hardware_Instructions.md  This bring-up guide
    ├── Architecture.md           This document
    ├── Serial_master_node_architecture.md   Detailed architecture reference
    └── RS485_VERIFICATION_PLAN.md           Hardware test plan (RS485, not currently used)
```