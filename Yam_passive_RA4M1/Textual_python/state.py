"""Pure state dataclasses.

No I/O, no Textual imports — safe to import from anywhere. All derived
quantities live on @property so consumers see consistent reads.
"""
from dataclasses import dataclass, field
from enum import IntEnum


class NodeState(IntEnum):
    UNSEEN = 0
    DISCOVERED = 1
    ONLINE = 2
    MISSING = 3
    OFFLINE = 4


# Firmware emits abbreviated state names ("DISC" not "DISCOVERED") to fit
# fixed-width NODE columns. The map is one-way Pythonward.
STATE_FROM_STR = {
    "UNSEEN":  NodeState.UNSEEN,
    "DISC":    NodeState.DISCOVERED,
    "ONLINE":  NodeState.ONLINE,
    "MISSING": NodeState.MISSING,
    "OFFLINE": NodeState.OFFLINE,
}
STATE_NAME = {
    NodeState.UNSEEN:     "UNSEEN",
    NodeState.DISCOVERED: "DISC",
    NodeState.ONLINE:     "ONLINE",
    NodeState.MISSING:    "MISSING",
    NodeState.OFFLINE:    "OFFLINE",
}
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
        if self.state == "BUS_OFF":
            return "FAIL"
        if self.state in ("PASSIVE", "WARNING") or self.rx_overrun > 0:
            return "WARN"
        if self.tec > 0 or self.rec > 0:
            return "WARN"
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
    nodes: dict = field(default_factory=dict)         # int -> NodeRow
    events: list = field(default_factory=list)        # list[EventEntry]
    conn: ConnectionStats = field(default_factory=ConnectionStats)
    paused: bool = False
    current_level: int = 2

    def push_event(self, ev: EventEntry, max_events: int = 200) -> None:
        self.events.append(ev)
        if len(self.events) > max_events:
            del self.events[0:len(self.events) - max_events]
