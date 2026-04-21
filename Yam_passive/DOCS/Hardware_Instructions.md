# YAM Passive Encoder Network — Hardware Instructions

Bring-up guide for the 7-node absolute encoder network. Covers parts, wiring, flashing, and verification.

---

## Prerequisites

### Hardware

| Qty | Part | Notes |
|-----|------|-------|
| 7 | Seeed XIAO RP2040 | One per joint, nodes 1–7 |
| 1 | Teensy 4.1 | Master controller |
| 7 | MT6701 magnetic encoder breakout | [Amazon B0DY7QV2QT](https://www.amazon.com/dp/B0DY7QV2QT) |
| 1 | FTDI USB-to-Serial adapter (3.3V) | Bridges Teensy Serial8 to PC |
| 1 | 5V power supply | Powers all XIAOs via 5V rail → 3.3V regulators |
| — | Hookup wire, common GND bus | UART connections between Teensy and each XIAO |

### Software

| Tool | Purpose |
|------|---------|
| Arduino IDE | Flash both MCU platforms |
| Seeed XIAO RP2040 board package | Board support for nodes (install via Arduino Board Manager) |
| Teensyduino | Board support for Teensy 4.1 |
| Python 3 + pyserial | Host PC receiver (`pip install pyserial`) |

### Protocol Library Setup

The `shared/` directory contains the `YamProtocol` Arduino library used by both node and master firmware. It must be accessible to the Arduino IDE:

```bash
ln -s /home/alex/Arduino/Verlet_Robotics/Yam_passive/shared ~/Arduino/libraries/YamProtocol
```

Verify: Arduino IDE → Sketch → Include Library → should list "YamProtocol".

---

## Power

- All 7 XIAOs are powered from a shared **5V rail**. Each XIAO's onboard regulator steps down to 3.3V.
- The Teensy 4.1 is powered via USB during development.
- **All boards must share a common GND.**

---

## Node Setup (Repeat for Each Node, 1–7)

Nodes are numbered **1 from the base** of the arm, incrementing toward the end effector (node 7).

### Step 1: Verify Encoder SPI Connection

Before flashing the network firmware, verify each MT6701 encoder independently.

1. Open `/home/alex/Arduino/Verlet_Robotics/mt6701/mt6701_spi_read/mt6701_spi_read.ino` in Arduino IDE
2. Select board: **Seeed XIAO RP2040**
3. Confirm the **chip select (CS) pin** in the sketch matches your wiring [6 or 7]
4. Upload and open Serial Monitor at **115200 baud**
5. Verify:
   - Angle value changes when you rotate the magnet
   - Field status reads **NORMAL** (not WEAK, STRONG, or ERR)
   - Raw hex bytes are not all `0xFF` or `0x00` (indicates disconnection)

If angle reads are stuck or field status is ERR, check:
- CS pin assignment matches your physical wiring
- SPI mode is **MODE1** (CPOL=0, CPHA=1) — MODE0 will not work
- Magnet is present and within range of the MT6701 IC

### Step 2: Flash seed_node Firmware

1. Open `/home/alex/Arduino/Verlet_Robotics/Yam_passive/seed_node/seed_node.ino`
2. Edit `seed_node/config.h`:
   - Set `NODE_ID` to this node's number (1–7, starting from base)
   - Confirm `SPI_CS_PIN` matches your hardware wiring
3. Select board: **Seeed XIAO RP2040**
4. Upload
5. Open Serial Monitor (115200) — verify startup message and angle readings

**Repeat Steps 1–2 for all 7 nodes** before proceeding. Each node must have a unique `NODE_ID`.

#### XIAO RP2040 Node Pinout

| Function | GPIO | Silkscreen | Direction |
|----------|------|------------|-----------|
| UART TX (to Teensy) | 0 | D6 | Output |
| UART RX (from Teensy) | 1 | D7 | Input |
| MT6701 SCK | 2 | D8 | Output |
| MT6701 MISO | 4 | D9 | Input |
| MT6701 CS | 6 | D5 | Output |
| User LED | 25 | — | Output |

SPI connections to the MT6701 are internal to the arm and not exposed.

---

## Teensy Master Setup

### Step 3: Flash teensy_master Firmware

1. Open `/home/alex/Arduino/Verlet_Robotics/Yam_passive/teensy_master/teensy_master.ino`
2. Select board: **Teensy 4.1**
3. Select USB Type: **Serial**
4. Upload

#### Teensy 4.1 Master Pinout

| Connection | Serial Port | TX Pin | RX Pin |
|------------|-------------|--------|--------|
| Node 1 | Serial1 | 1 | 0 |
| Node 2 | Serial2 | 8 | 7 |
| Node 3 | Serial3 | 14 | 15 |
| Node 4 | Serial4 | 17 | 16 |
| Node 5 | Serial5 | 20 | 21 |
| Node 6 | Serial6 | 24 | 25 |
| Node 7 | Serial7 | 29 | 28 |
| Host PC (FTDI) | Serial8 | 35 | 34 |
| USB Debug | Serial (USB) | — | — |

**Wiring:** Each XIAO connects to one Teensy serial port (point-to-point). Cross TX/RX:
- Teensy TX pin → XIAO RX pin (GPIO1 / D7)
- Teensy RX pin ← XIAO TX pin (GPIO0 / D6)
- Common GND between boards

All serial ports run at **115200 baud**.

### Step 4: Verify Serial Connections (Isolated)

Test each node individually before running the full system.

1. Open `/home/alex/Arduino/Verlet_Robotics/Yam_passive/master_test/master_test.ino`
2. Upload to Teensy 4.1
3. Open Serial Monitor (115200)
4. Use CLI commands:
   - `s` — send GET_SAMPLE to node, verify angle/velocity response
   - `d` — send GET_DIAG, verify uptime and error counters
   - `p` — send PING, verify heartbeat response
   - `l` — enter loop mode (continuous 100 Hz polling with success stats)
   - `x` — stop loop mode
5. Confirm valid responses for each node's serial port

If a node does not respond:
- Check TX/RX crossover wiring (TX→RX, RX→TX)
- Check that NODE_ID in the node's `config.h` matches expectations
- Verify baud rate is 115200 on both ends
- Check common GND

### Step 5: Run Full System

1. Re-upload `teensy_master.ino` to Teensy 4.1 (if master_test is still loaded)
2. Open Serial Monitor (115200)
3. The dashboard displays all node states at 1 Hz
4. CLI commands:
   - `s` — status summary
   - `d` — diagnostics
   - `p` — pause/resume polling
   - `r` — reset counters
   - `?` — help
5. Verify: all nodes reporting, ~100 Hz poll rate, 0% timeout rate

---

## Host PC Setup

### Step 6: Connect FTDI and Run host_receiver

1. Wire the FTDI adapter to Teensy Serial8:
   - Teensy TX (pin 35) → FTDI RX
   - Teensy RX (pin 34) ← FTDI TX
   - Common GND
   - FTDI must be **3.3V logic** (Teensy 4.1 is 3.3V)
2. Plug FTDI into PC USB
3. Run the host receiver:

```bash
python /home/alex/Arduino/Verlet_Robotics/Yam_passive/host_receiver/host_receiver.py \
    --port /dev/ttyUSB0 \
    --baud 115200
```

4. Optional flags:
   - `--log data.csv` — record to CSV file
   - `--raw` — show raw hex dump
   - `--quiet` — suppress real-time display
5. Ctrl+C to stop — prints session statistics

---

## Troubleshooting

| Symptom | Check |
|---------|-------|
| No SPI readings / angle stuck at 0 | CS pin correct? SPI MODE1? Magnet present? |
| Raw bytes all `0xFF` or `0x00` | Encoder disconnected — check SPI wiring |
| Field status WEAK/STRONG | Adjust magnet distance from MT6701 IC |
| Node not responding to master | TX/RX crossover correct? NODE_ID matches? Baud 115200? Common GND? |
| CRC errors on master | Check wire length, common GND, baud rate match |
| Host receiver no data | FTDI wiring correct (TX↔RX crossover)? Correct port (`/dev/ttyUSB0`)? |
| Host receiver CRC errors | Baud rate mismatch, or electrical noise on FTDI wiring |
| Dashboard shows "FAULT" for a node | 100+ consecutive missed polls — check that node's UART connection |
| Node shows "SENSOR_FAULT" | 500+ consecutive SPI failures — check encoder wiring on that node |