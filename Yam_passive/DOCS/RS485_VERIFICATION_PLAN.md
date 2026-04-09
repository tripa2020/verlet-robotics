# RS-485 Two-Node Hardware Verification Plan

**Project:** Yam Passive Encoder Network
**Goal:** Verify RS-485 hardware topology works with 2 nodes before scaling to 6
**Date:** 2024-04-01

---

## Overview

This plan verifies the foundational hardware topology:
- Teensy 4.1 master polling RP2040 nodes over RS-485
- Each node reads an MT6701 absolute encoder via SPI
- ASCII protocol for easy debugging

This is NOT the production system. It validates hardware before adding:
- Binary framing with CRC
- 500 Hz node-local acquisition
- Health state machines
- Production error recovery

---

## Hardware Bill of Materials

| Item | Qty | Notes |
|------|-----|-------|
| Teensy 4.1 | 1 | Master controller |
| RP2040-Zero | 2 | Node controllers |
| XIAO RP2040 | 2 | Alternative node controller (smaller form factor) |
| MT6701 | 2 | Absolute magnetic encoder |
| TTL-RS485 module (auto-direction) | 3 | One per MCU |
| Twisted pair cable | 1 | A/B differential pair + GND |
| 120 ohm resistor | 2 | Bus termination (master + far node) |

---

## Pin Assignments

### RP2040 Node

| Function | GPIO | Notes |
|----------|------|-------|
| RS-485 TX | 16 | UART0 remapped |
| RS-485 RX | 17 | UART0 remapped |
| MT6701 MISO (DO) | 0 | SPI0 |
| MT6701 CS (CSN) | 1 | SPI0, directly controlled |
| MT6701 SCK (CLK) | 2 | SPI0 |

### XIAO RP2040 Node (Primary)

| Function | GPIO | Silkscreen | Notes |
|----------|------|------------|-------|
| RS-485 TX | 0 | D6 | UART0 TX (default) |
| RS-485 RX | 1 | D7 | UART0 RX (default) |
| MT6701 SCK (CLK) | 2 | D8 | SPI0 hardware |
| MT6701 MISO (DO) | 4 | D9 | SPI0 hardware |
| MT6701 CS (CSN) | 7 | D5 | Directly controlled |
| User LED | 25 | - | Accent LED for status |

### Teensy 4.1 Master

| Function | Pin | Notes |
|----------|-----|-------|
| RS-485 TX | 29 | Serial7 |
| RS-485 RX | 28 | Serial7 |
| USB Serial | - | Debug/command interface |

### RS-485 Module Wiring (per MCU)

**If module has DI/RO labels:**
```
MCU TX  ────────► DI  (Data In)
MCU RX  ◄──────── RO  (Receiver Out)
```

**If module has TX/RX labels (crossed like serial):**
```
MCU TX  ────────► Module RX
MCU RX  ◄──────── Module TX
```

**Power and bus:**
```
3.3V    ────────► VCC
GND     ────────► GND
                  A ───┐
                  B ───┼──► RS-485 bus
```

### RS-485 Bus Topology

```
┌─────────────┐         ┌─────────────┐         ┌─────────────┐
│   Teensy    │         │   Node 1    │         │   Node 2    │
│   Master    │         │  (ID = 1)   │         │  (ID = 2)   │
└──────┬──────┘         └──────┬──────┘         └──────┬──────┘
       │                       │                       │
    ┌──┴──┐                 ┌──┴──┐                 ┌──┴──┐
    │RS485│                 │RS485│                 │RS485│
    │ mod │                 │ mod │                 │ mod │
    └──┬──┘                 └──┬──┘                 └──┬──┘
       │                       │                       │
   A ──┼───────────────────────┼───────────────────────┼── A
   B ──┼───────────────────────┼───────────────────────┼── B
 GND ──┼───────────────────────┼───────────────────────┼── GND
       │                       │                       │
    [120Ω]                                          [120Ω]
   terminator                                      terminator
```

---

## Protocol Specification (ASCII v1)

### Commands (Master → Node)

| Command | Format | Description |
|---------|--------|-------------|
| PING | `PING,<id>\n` | Health check, node responds with PONG |
| GET | `GET,<id>\n` | Request current encoder angle |

### Responses (Node → Master)

| Response | Format | Description |
|----------|--------|-------------|
| PONG | `PONG,<id>\n` | Acknowledges PING |
| ANGLE | `ANGLE,<id>,<raw>\n` | 14-bit raw encoder count (0-16383) |

### Timing Parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| Baud rate | 115200 | Both UART and RS-485 |
| Reply timeout | 50 ms | Master waits this long for response |
| Poll interval | 10 ms | 100 Hz total (50 Hz per node in 2-node test) |

---

## Test Sequence

### Test 1: Node USB Sanity Check

**Purpose:** Verify RP2040 firmware runs and MT6701 SPI works before RS-485

**Procedure:**
1. Flash `rp2040_node.ino` to RP2040 (NODE_ID = 1)
2. Connect RP2040 USB to computer
3. Open serial monitor at 115200 baud
4. Observe startup message: `Node 1 ready. RS485 on GPIO16/17, MT6701 on SPI0`

**Success Criteria:**
- Startup message appears
- No crash or hang

**Failure Modes:**
- No output → check USB connection, verify correct board selected in IDE
- Crash/hang → check SPI pin conflicts, verify MT6701 wiring

---

### Test 2: Master USB Sanity Check

**Purpose:** Verify Teensy firmware runs before connecting RS-485

**Procedure:**
1. Flash `teensy_master.ino` to Teensy 4.1
2. Open serial monitor at 115200 baud
3. Observe startup message and command menu

**Success Criteria:**
- Displays "Teensy RS-485 Master Ready"
- Shows command help

**Failure Modes:**
- No output → check USB connection
- Wrong baud rate selected in monitor

---

### Test 3: Single-Node PING Over RS-485

**Purpose:** Verify RS-485 physical layer and basic protocol

**Setup:**
1. Wire Teensy RS-485 module to Node 1 RS-485 module (A-A, B-B, GND-GND)
2. Add 120 ohm termination at both ends (or skip for short cable <1m)
3. Power both boards

**Procedure:**
1. Open Teensy serial monitor
2. Type `p1` and press Enter

**Success Criteria:**
- Output: `Node 1: PONG OK`

**Failure Modes:**

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| `Node 1: TIMEOUT` | No RS-485 connection | Check A/B wiring, verify module powered |
| `Node 1: TIMEOUT` | TX/RX swapped | Swap TX↔RX at one end |
| `Node 1: TIMEOUT` | Wrong UART pins | Verify GPIO16/17 on RP2040, pins 28/29 on Teensy |
| `Node 1: Bad reply 'PONG,2'` | Wrong NODE_ID flashed | Reflash with correct NODE_ID |
| Garbage characters | Baud rate mismatch | Verify both at 115200 |

---

### Test 4: Single-Node GET Angle

**Purpose:** Verify MT6701 SPI read works over RS-485 path

**Procedure:**
1. Type `g1` in Teensy serial monitor

**Success Criteria:**
- Output: `Node 1: angle=XXXX (YY.YY deg)`
- Angle value changes when magnet is rotated

**Failure Modes:**

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| `angle=0` always | MT6701 not connected | Check SPI wiring |
| `angle=16383` always | MT6701 CS stuck low | Check CS pin (GPIO1) |
| Angle doesn't change | Magnet not over sensor | Position magnet correctly |
| Angle jumps erratically | Weak magnetic field | Check magnet placement, MT6701 field status |

---

### Test 5: Two-Node Addressing

**Purpose:** Verify multi-drop RS-485 with address filtering

**Setup:**
1. Flash second RP2040 with `NODE_ID = 2`
2. Wire Node 2 RS-485 module to same bus (A-A, B-B, GND-GND)
3. Move termination resistor from Node 1 to Node 2 (far end)

**Procedure:**
1. Type `p1` → should get `Node 1: PONG OK`
2. Type `p2` → should get `Node 2: PONG OK`
3. Type `a` → should get angles from both nodes

**Success Criteria:**
- Each node responds only to its own ID
- No collisions or garbled responses
- Both angles read correctly

**Failure Modes:**

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| Both nodes respond to same ID | Duplicate NODE_ID | Reflash one node with correct ID |
| One node works, other times out | Wiring to second node | Check Node 2 RS-485 connections |
| Garbled responses | Bus contention | Should not happen with polling - check node firmware |

---

### Test 6: Continuous Polling Stress Test

**Purpose:** Verify sustained operation at target polling rate

**Procedure:**
1. Type `r` to start continuous polling (50 Hz per node)
2. Let run for 60 seconds minimum
3. Type `s` to stop
4. Observe statistics

**Success Criteria:**
- Success rate > 99% for both nodes
- No timeouts trending upward over time
- Angles continue updating (not stuck)

**Timing Budget:**
```
Poll interval:     10 ms per node
Command TX time:   ~1 ms (8 chars @ 115200)
Node processing:   ~0.5 ms (SPI read + format)
Response TX time:  ~1.5 ms (20 chars @ 115200)
Margin:            ~7 ms
```

**Failure Modes:**

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| 80-90% success rate | Timing too tight | Increase REPLY_TIMEOUT_MS |
| Gradually increasing timeouts | Buffer overflow | Check rxBuffer sizes |
| Periodic timeout bursts | EMI interference | Check cable routing, add shielding |
| One node consistently worse | Bad connection | Check that specific node's wiring |

---

### Test 7: Extended Soak Test

**Purpose:** Verify stability over longer duration

**Procedure:**
1. Start continuous polling with `r`
2. Run for 1 hour
3. Periodically check statistics with `?`
4. Stop with `s` and record final stats

**Success Criteria:**
- Success rate > 99.5% sustained
- No crashes or hangs
- Memory stable (no leaks visible in timing degradation)

**Log Format:**
```
Start time: ____________
End time:   ____________
Duration:   ____________

Node 1: polls=______ success=______ timeout=______ (___._%)
Node 2: polls=______ success=______ timeout=______ (___._%)

Notes:
```

---

## Troubleshooting Quick Reference

### RS-485 Signal Issues

| Check | Method |
|-------|--------|
| A/B polarity | Swap A↔B if no communication |
| Termination | 120Ω between A and B at each end |
| Ground | Common GND between all devices |
| Bias resistors | Only at master if using external biasing |

### Oscilloscope Checkpoints

| Signal | Expected |
|--------|----------|
| TX line (before RS-485 module) | TTL levels, data visible |
| A-B differential | ±2V to ±5V swings |
| Idle bus state | Defined (not floating) |

### Common Mistakes

1. **TX/RX swapped** - Most common issue
2. **Wrong NODE_ID** - Flashed same firmware to both nodes
3. **SPI pins conflict** - Some RP2040 boards have different default SPI pins
4. **Auto-direction timing** - Some modules need brief delay after TX

---

## Next Steps After Verification Passes

Once all tests pass with >99% reliability:

1. **Binary Protocol** - Replace ASCII with fixed-length binary packets + CRC8
2. **Sequence Numbers** - Detect missed/duplicate responses
3. **Health Counters** - Track consecutive failures, sensor faults
4. **500 Hz Local Sampling** - Timer ISR on node, report latest sample
5. **Scale to 6 Nodes** - Add remaining 4 nodes incrementally
6. **Production Startup** - Require all nodes present before enabling

---

## Files Reference

| File | Purpose |
|------|---------|
| `seed_node/seed_node.ino` | XIAO RP2040 node firmware (change NODE_ID before flashing) |
| `rp2040_node/rp2040_node.ino` | RP2040-Zero node firmware (legacy) |
| `teensy_master/teensy_master.ino` | Master firmware with auto-ping |
| `mt6701/mt6701_spi_read/mt6701_spi_read.ino` | Standalone MT6701 diagnostic (XIAO RP2040) |
| `DOCS/RS485_VERIFICATION_PLAN.md` | This document |

---

## Version History

| Date | Change |
|------|--------|
| 2024-04-01 | Initial plan for 2-node hardware verification |
| 2026-04-02 | Added XIAO RP2040 pin assignments and file reference |
