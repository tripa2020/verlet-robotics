# Verlet Robotics

Firmware and host software for the **YAM passive robotic arm** — a 7-DOF encoder network that streams joint angles to a host PC at 100 Hz.

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

Each joint has an **MT6701 magnetic absolute encoder** read by a dedicated **Seeed XIAO RP2040**. A **Teensy 4.1** master polls all nodes over point-to-point UART and streams aggregated angle + velocity data to a host PC. A PyBullet digital twin mirrors the arm in real time.

## Hardware

| Qty | Part | Role |
|-----|------|------|
| 7 | Seeed XIAO RP2040 | Node MCU (one per joint) |
| 1 | Teensy 4.1 | Master controller |
| 7 | MT6701 magnetic encoder | Absolute angle sensing |
| 1 | FTDI USB-to-Serial (3.3V) | Master ↔ PC bridge |

## Repository Structure

```
Verlet_Robotics/
├── Yam_passive/              ← Main project
│   ├── seed_node/            Node firmware (XIAO RP2040)
│   ├── teensy_master/        Master firmware (Teensy 4.1)
│   ├── shared/               YamProtocol Arduino library (used by both)
│   ├── host_receiver/        Python host-side frame parser + logger
│   ├── Pybullet/             Digital twin viewer + i2rt library
│   ├── master_test/          Single-node interactive tester
│   └── DOCS/                 Detailed documentation (see below)
│
├── mt6701/                   Standalone MT6701 driver tests + CRC debug
├── RP2040/                   RP2040-specific MT6701 SPI read test
└── RP2040_test/              Bare RP2040 board test
```

## Quick Start

### Prerequisites

- [Arduino IDE](https://www.arduino.cc/en/software) with:
  - Seeed XIAO RP2040 board package
  - Teensyduino
- Python 3 + `pyserial` (`pip install pyserial`)
- For the digital twin: `pip install pybullet`

### Protocol Library Setup

The `shared/` directory is an Arduino library used by both node and master firmware. Symlink it into your Arduino libraries folder:

```bash
ln -s $(pwd)/Yam_passive/shared ~/Arduino/libraries/YamProtocol
```

### Flashing

1. Flash each XIAO RP2040 with `Yam_passive/seed_node/` (set `NODE_ID` in `config.h` per joint, 1–7)
2. Flash the Teensy 4.1 with `Yam_passive/teensy_master/`
3. Run `python Yam_passive/host_receiver/host_receiver.py` to see streaming data

See [Yam_passive/DOCS/](Yam_passive/DOCS/) for full instructions.

## Documentation

Detailed docs live in [`Yam_passive/DOCS/`](Yam_passive/DOCS/):

| Document | Contents |
|----------|----------|
| [Architecture.md](Yam_passive/DOCS/Architecture.md) | System design, data flow, binary protocol, timing, fault handling |
| [Hardware_Instructions.md](Yam_passive/DOCS/Hardware_Instructions.md) | Parts list, wiring, flashing, verification — full bring-up guide |
| [\[Devel\] Serial Master Node Architecture](Yam_passive/DOCS/%5BDevel%5D_Serial_master_node_architecture.md) | Development log with implementation status and design decisions |

## Key Specs

- **7 nodes**, one per joint (base → end effector)
- **500 Hz** local sampling per node (EMA filtered, wraparound-safe)
- **100 Hz** master poll rate, 100 Hz host stream
- **CRC8** on all protocol frames, **CRC6** on MT6701 SPI reads
- **Freeze-not-zero** fault policy — faulted nodes hold last valid data with status flags
- **< 2 ms** round-trip per node