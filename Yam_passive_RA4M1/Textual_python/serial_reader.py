"""Threaded serial I/O. Pure I/O — no presentation, no state logic.

Reads lines from the Teensy, hands them to `on_line` callback (called from
the reader thread). Writes commands via `send()`.

Port auto-detect filters by Teensy USB VID 0x16C0 (PJRC).
"""
import threading
import time
from typing import Callable, Optional

import serial
from serial.tools import list_ports

TEENSY_VID = 0x16C0


def find_teensy_port(explicit: Optional[str] = None) -> str:
    """Resolve the Teensy serial port.

    - If `explicit` given, return it.
    - If exactly one Teensy is enumerated, use it silently.
    - If none, raise SystemExit with a clear message.
    - If multiple, list them and require an explicit `--port`.
    """
    if explicit:
        return explicit
    matches = [p for p in list_ports.comports() if p.vid == TEENSY_VID]
    if len(matches) == 1:
        return matches[0].device
    if not matches:
        raise SystemExit(
            f"No Teensy found (VID 0x{TEENSY_VID:04X}). "
            "Plug it in, or pass --port /dev/ttyACMx."
        )
    devs = ", ".join(p.device for p in matches)
    raise SystemExit(f"Multiple Teensies found ({devs}). Pass --port to pick one.")


class SerialReader:
    """Background-thread serial reader. Lines pushed to `on_line` callback."""

    def __init__(self, port: str, baud: int,
                 on_line: Callable[[str], None],
                 on_error: Callable[[str], None]):
        self.port_name = port
        self.baud = baud
        self.on_line = on_line
        self.on_error = on_error
        self._ser: Optional[serial.Serial] = None
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()

    def start(self) -> None:
        try:
            self._ser = serial.Serial(self.port_name, self.baud, timeout=0.1)
        except serial.SerialException as e:
            self.on_error(f"open {self.port_name}: {e}")
            return
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._ser is not None and self._ser.is_open:
            try:
                self._ser.close()
            except Exception:
                pass

    def send(self, payload: bytes) -> None:
        try:
            if self._ser is not None and self._ser.is_open:
                self._ser.write(payload)
        except serial.SerialException as e:
            self.on_error(f"write: {e}")

    def _loop(self) -> None:
        assert self._ser is not None
        buf = b""
        while not self._stop.is_set():
            try:
                chunk = self._ser.read(256)
                if chunk:
                    buf += chunk
                    while b"\n" in buf:
                        line, _, buf = buf.partition(b"\n")
                        text = line.decode("ascii", errors="replace").rstrip("\r")
                        if text:
                            self.on_line(text)
            except serial.SerialException as e:
                self.on_error(f"read: {e}")
                time.sleep(0.5)
            except Exception as e:
                self.on_error(f"reader: {e}")
                time.sleep(0.5)
