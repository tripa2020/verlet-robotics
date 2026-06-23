# YAM Passive — Debug TUI

Live debug viewer for the Teensy 4.1 master in `Yam_passive_RA4M1`.

Pure presentation in Textual + Rich. State, parsing, and serial I/O are
strictly separated — see file headers.

## Install

```
pip install pyserial textual rich
```

## Run

```
python main.py                  # auto-detects Teensy by USB VID 0x16C0
python main.py --port /dev/ttyACM0
```

## Keybinds

| Key   | Action                                |
|-------|---------------------------------------|
| `0`   | Master silent (`SET level=0`)         |
| `1`   | Events only (`SET level=1`)           |
| `2`   | Events + 1 Hz summary (default)       |
| `3`   | + per-cycle trace (`SET level=3`)     |
| `c`   | Clear event log + reset master throttle |
| `r`   | Reset master counters                 |
| `p`   | Pause UI refresh (data still buffers) |
| `q`   | Quit                                  |

## File layout

| File              | Role                                              |
|-------------------|---------------------------------------------------|
| `protocol.py`     | Wire format constants + pure `parse_line()`        |
| `state.py`        | `LiveState` dataclasses + derived `@property`      |
| `serial_reader.py`| Threaded serial I/O + Teensy port auto-detect      |
| `widgets.py`      | Pure presentation Rich widgets                     |
| `main.py`         | Textual `App` — wires everything together          |
| `yam.tcss`        | Textual stylesheet                                 |

## Wire format

ASCII key=value lines, line-delimited, type-tagged. Full spec in
`../DOCS/Error_handling.md` §5. Sample:

```
STAT t=12345 poll=1234 tec=0 rec=0 esr=0000 bus=ACTIVE sync=1234 polltx=1234 init=12 mbb=0 rxv=4920 rxu=0 rxs=0 ovr=0 slip=0 jit=23
NODE id=1 st=ONLINE rxs=100 rxt=12345 miss=0 lat=180 ang=45.2 vel=0.05 fl=V
EVT  t=4523 sev=INFO  cls=NODE node=4 msg="first reply at t=4523ms after 4 INIT retries"
```
