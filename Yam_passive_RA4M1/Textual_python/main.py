"""Textual app entry — wires reader + state + widgets + bindings.

Run:
    python main.py                 # auto-detect Teensy
    python main.py --port /dev/ttyACM0

Keybinds:
    0..3   set master verbosity (silent/events/summary/trace)
    c      clear event log + reset master throttle
    r      reset master counters
    p      pause UI refresh
    q      quit
"""
import argparse

from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Vertical
from textual.widgets import Header, Footer

from state import (
    LiveState, NodeRow, EventEntry, NodeState, STATE_FROM_STR,
)
from protocol import (
    parse_line, cmd_set_level, cmd_reset_counters,
    cmd_reset_events, SEV_FROM_STR,
)
from serial_reader import SerialReader, find_teensy_port
from widgets import BusPanel, NodesPanel, EventsPanel, StatusBar


class YamDebugApp(App):
    CSS_PATH = "yam.tcss"
    BINDINGS = [
        Binding("q", "quit",                "Quit"),
        Binding("0", "set_level('0')",      "Silent"),
        Binding("1", "set_level('1')",      "Events"),
        Binding("2", "set_level('2')",      "Summary"),
        Binding("3", "set_level('3')",      "Trace"),
        Binding("c", "clear_events",        "Clear"),
        Binding("r", "reset_counters",      "Reset cnt"),
        Binding("p", "toggle_pause",        "Pause"),
    ]

    def __init__(self, port: str, baud: int = 115200):
        super().__init__()
        self.state = LiveState()
        self.state.conn.port = port
        self.reader = SerialReader(port, baud,
                                   on_line=self._on_line,
                                   on_error=self._on_serial_err)

    def compose(self) -> ComposeResult:
        yield Header(name="YAM Passive — CAN Encoder Debug")
        with Vertical():
            yield BusPanel(id="bus")
            yield NodesPanel(id="nodes")
            yield EventsPanel(id="events")
        yield StatusBar(id="status")
        yield Footer()

    def on_mount(self) -> None:
        self.reader.start()
        self.state.conn.connected = True
        self.set_interval(0.1, self._refresh)   # 10 Hz UI refresh

    def on_unmount(self) -> None:
        self.reader.stop()

    # --- refresh ---------------------------------------------------------
    def _refresh(self) -> None:
        if self.state.paused:
            return
        self.query_one("#bus",    BusPanel).update_state(self.state.bus)
        self.query_one("#nodes",  NodesPanel).update_state(self.state.nodes)
        self.query_one("#events", EventsPanel).update_state(self.state.events)
        self.query_one("#status", StatusBar).update_state(
            self.state.conn, self.state.current_level, self.state.paused
        )

    # --- ingestion (called from reader thread) ---------------------------
    def _on_line(self, line: str) -> None:
        self.state.conn.rx_lines += 1
        p = parse_line(line)
        try:
            if   p.kind == "STAT": self._apply_stat(p.fields)
            elif p.kind == "NODE": self._apply_node(p.fields)
            elif p.kind == "EVT":  self._apply_evt(p.fields)
            elif p.kind in ("ACK", "PONG", "NAK", "CFG", "UNKNOWN"):
                # ACK/PONG/CFG are confirmation — display in events at INFO level
                if p.kind in ("NAK",):
                    self.state.push_event(EventEntry(
                        t_ms=self.state.bus.last_t_ms,
                        severity=SEV_FROM_STR["WARN"], cls="SYSTEM",
                        msg=f"NAK {p.fields.get('cmd','?')} {p.fields.get('reason','')}",
                    ))
            else:
                self.state.conn.parse_errors += 1
        except Exception:
            self.state.conn.parse_errors += 1

    def _on_serial_err(self, msg: str) -> None:
        self.state.conn.serial_errors += 1
        self.state.conn.connected = False

    # --- field appliers --------------------------------------------------
    def _apply_stat(self, f: dict) -> None:
        b = self.state.bus
        b.last_t_ms     = _int(f, "t",     b.last_t_ms)
        b.poll_count    = _int(f, "poll",  b.poll_count)
        b.tec           = _int(f, "tec",   b.tec)
        b.rec           = _int(f, "rec",   b.rec)
        b.esr1          = int(f.get("esr", "0"), 16)
        b.state         = f.get("bus", b.state)
        b.sync_tx       = _int(f, "sync",   b.sync_tx)
        b.poll_tx       = _int(f, "polltx", b.poll_tx)
        b.init_tx       = _int(f, "init",   b.init_tx)
        b.mb_busy       = _int(f, "mbb",    b.mb_busy)
        b.rx_valid      = _int(f, "rxv",    b.rx_valid)
        b.rx_unknown    = _int(f, "rxu",    b.rx_unknown)
        b.rx_short      = _int(f, "rxs",    b.rx_short)
        b.rx_overrun    = _int(f, "ovr",    b.rx_overrun)
        b.cycle_slip    = _int(f, "slip",   b.cycle_slip)
        b.max_jitter_us = _int(f, "jit",    b.max_jitter_us)

    def _apply_node(self, f: dict) -> None:
        nid = _int(f, "id", 0)
        if nid == 0:
            return
        n = self.state.nodes.setdefault(nid, NodeRow(node_id=nid))
        n.state           = STATE_FROM_STR.get(f.get("st", "UNSEEN"), NodeState.UNSEEN)
        n.rx_per_sec      = _int(f, "rxs", 0)
        n.rx_total        = _int(f, "rxt", 0)
        n.miss_count      = _int(f, "miss", 0)
        lat = f.get("lat", "-")
        n.last_latency_us = int(lat) if lat != "-" else 0
        ang = f.get("ang", "-")
        n.angle_deg       = float(ang) if ang != "-" else 0.0
        vel = f.get("vel", "-")
        n.velocity        = float(vel) if vel != "-" else 0.0
        n.flags           = f.get("fl", "-")

    def _apply_evt(self, f: dict) -> None:
        sev_raw = f.get("sev", "INFO").strip()
        ev = EventEntry(
            t_ms=_int(f, "t", 0),
            severity=SEV_FROM_STR.get(sev_raw, 0),
            cls=f.get("cls", "?"),
            msg=f.get("msg", ""),
            node=_int(f, "node", 0),
        )
        self.state.push_event(ev)

    # --- actions ---------------------------------------------------------
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


def _int(d: dict, key: str, default: int) -> int:
    """Tolerant int parser — returns default on missing or non-numeric."""
    v = d.get(key)
    if v is None or v == "-":
        return default
    try:
        return int(v)
    except (ValueError, TypeError):
        return default


def main() -> None:
    ap = argparse.ArgumentParser(description="YAM CAN debug TUI")
    ap.add_argument("--port", help="Serial port (auto-detected if omitted)")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()
    port = find_teensy_port(args.port)
    YamDebugApp(port, args.baud).run()


if __name__ == "__main__":
    main()
