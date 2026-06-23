"""Pure presentation widgets.

Consume LiveState dataclasses and render via Rich. No I/O, no parsing —
mirrors the tripp-teleop `build_display(state)` pattern.
"""
from textual.widgets import Static
from rich.table import Table
from rich.text import Text
from rich.panel import Panel
from rich import box

from state import (
    BusStats, NodeRow, EventEntry, ConnectionStats,
    NodeState, STATE_NAME, STATE_STYLE, SEV_NAME, SEV_STYLE,
)


def _bus_state_style(state: str) -> str:
    if state == "ACTIVE":  return "green"
    if state in ("WARNING", "PASSIVE"): return "yellow"
    if state == "BUS_OFF": return "bold red"
    return "dim"


class BusPanel(Static):
    def update_state(self, b: BusStats) -> None:
        t = Table.grid(padding=(0, 2))
        t.add_column(style="bold", min_width=16)
        t.add_column()
        t.add_row("Bus state", Text(b.state, style=_bus_state_style(b.state)))
        t.add_row("TEC / REC", f"{b.tec} / {b.rec}")
        t.add_row("ESR1", f"0x{b.esr1:04X}")
        t.add_row("TX SYNC/POLL/INIT", f"{b.sync_tx} / {b.poll_tx} / {b.init_tx}")
        t.add_row("TX mb_busy", str(b.mb_busy))
        t.add_row("RX v/u/s/ovr",
                  f"{b.rx_valid} / {b.rx_unknown} / {b.rx_short} / {b.rx_overrun}")
        t.add_row("Cycle slip / jitter", f"{b.cycle_slip} / {b.max_jitter_us}µs")
        t.add_row("Poll count", str(b.poll_count))
        self.update(Panel(t, title="Bus", border_style="blue"))


class NodesPanel(Static):
    def update_state(self, nodes: dict) -> None:
        t = Table(box=box.SIMPLE_HEAD, expand=True)
        for col in ("ID", "State", "RX/s", "RX_total", "Miss%",
                    "Lat(µs)", "Ang(°)", "Vel(r/s)", "Fl"):
            t.add_column(col)
        for nid in range(1, 8):  # Always render all 7 rows so layout doesn't jump.
            n: NodeRow = nodes.get(nid)
            if n is None:
                t.add_row(str(nid), Text("UNSEEN", style="dim"),
                          "0", "0", "-", "-", "-", "-", "-")
                continue
            state_text = Text(STATE_NAME[n.state], style=STATE_STYLE[n.state])
            unseen = (n.state == NodeState.UNSEEN)
            t.add_row(
                str(n.node_id), state_text,
                str(n.rx_per_sec), str(n.rx_total),
                f"{n.miss_pct:.1f}" if not unseen else "-",
                str(n.last_latency_us) if n.last_latency_us else "-",
                f"{n.angle_deg:.1f}" if not unseen else "-",
                f"{n.velocity:+.2f}" if not unseen else "-",
                n.flags,
            )
        self.update(Panel(t, title="Nodes", border_style="blue"))


class EventsPanel(Static):
    MAX_VISIBLE = 20

    def update_state(self, events: list) -> None:
        t = Table.grid(padding=(0, 1))
        for _ in range(4):
            t.add_column()
        recent = events[-self.MAX_VISIBLE:]
        for ev in recent:
            sev = Text(SEV_NAME[ev.severity], style=SEV_STYLE[ev.severity])
            node_tag = f"N{ev.node}" if ev.node else ""
            t.add_row(f"{ev.t_ms}ms", sev,
                      Text(f"{ev.cls:<7} {node_tag}", style="bold"),
                      ev.msg)
        if not recent:
            t.add_row("", "", "", Text("(no events yet)", style="dim italic"))
        self.update(Panel(t, title="Events", border_style="blue"))


class StatusBar(Static):
    def update_state(self, c: ConnectionStats, level: int, paused: bool) -> None:
        line = Text()
        line.append(c.port, style="bold green" if c.connected else "bold red")
        line.append(f"   level={level}", style="cyan")
        line.append(f"   rx={c.rx_lines}", style="dim")
        line.append(f"   parse_err={c.parse_errors}",
                    style="red" if c.parse_errors else "dim")
        line.append(f"   serial_err={c.serial_errors}",
                    style="red" if c.serial_errors else "dim")
        if paused:
            line.append("   [PAUSED]", style="bold yellow")
        self.update(line)
