#!/usr/bin/env python3
"""
Host Receiver - MT6701 Encoder Network Stream

Receives and decodes binary frames from Teensy master via FTDI/Serial8.
Displays real-time encoder data from all nodes.

Usage:
    python host_receiver.py [--port /dev/ttyUSB0] [--baud 115200] [--log data.csv]

Frame Format (27 bytes for 2 nodes):
    START(1) + TIMESTAMP(4) + N_NODES(1) + NODE_DATA(10*N) + CRC(1)

Per-Node Data Block (10 bytes):
    NODE_ID(1) + ANGLE(4) + VELOCITY(4) + STATUS(1)

Ref: Serial_master_node_architecture.md Section 3.6
"""

import argparse
import struct
import sys
import time
from datetime import datetime

try:
    import serial
except ImportError:
    print("Error: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)

# Protocol constants
FRAME_START_HOST = 0xCC
STATUS_VALID = 0x01
STATUS_STALE = 0x02
STATUS_SENSOR_FAULT = 0x04
STATUS_FIELD_WEAK = 0x08
STATUS_FIELD_STRONG = 0x10
STATUS_CRC_ERROR = 0x20


def crc8(data: bytes) -> int:
    """CRC8 with polynomial 0x07, init 0x00"""
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def status_to_str(status: int) -> str:
    """Convert status flags to human-readable string"""
    flags = []
    if status & STATUS_VALID:
        flags.append("VALID")
    if status & STATUS_STALE:
        flags.append("STALE")
    if status & STATUS_SENSOR_FAULT:
        flags.append("FAULT")
    if status & STATUS_FIELD_WEAK:
        flags.append("WEAK")
    if status & STATUS_FIELD_STRONG:
        flags.append("STRONG")
    if status & STATUS_CRC_ERROR:
        flags.append("CRC_ERR")
    return "|".join(flags) if flags else "NONE"


def parse_host_frame(frame: bytes) -> dict:
    """Parse a host stream frame, returns dict with parsed data or None on error"""
    if len(frame) < 7:
        return None

    if frame[0] != FRAME_START_HOST:
        return None

    # Parse header
    timestamp = struct.unpack('<I', frame[1:5])[0]
    n_nodes = frame[5]

    # Calculate expected frame size
    expected_len = 6 + (n_nodes * 10) + 1
    if len(frame) != expected_len:
        return None

    # Verify CRC
    crc_received = frame[-1]
    crc_calc = crc8(frame[:-1])
    if crc_received != crc_calc:
        return None

    # Parse node data
    nodes = []
    offset = 6
    for _ in range(n_nodes):
        node_id = frame[offset]
        angle = struct.unpack('<f', frame[offset+1:offset+5])[0]
        velocity = struct.unpack('<f', frame[offset+5:offset+9])[0]
        status = frame[offset+9]
        nodes.append({
            'id': node_id,
            'angle_rad': angle,
            'angle_deg': angle * 180.0 / 3.14159265359,
            'velocity': velocity,
            'status': status,
            'status_str': status_to_str(status)
        })
        offset += 10

    return {
        'timestamp_us': timestamp,
        'n_nodes': n_nodes,
        'nodes': nodes
    }


def find_frame_start(ser: serial.Serial, timeout: float = 1.0) -> bool:
    """Scan for frame start byte, returns True if found"""
    start_time = time.time()
    while (time.time() - start_time) < timeout:
        if ser.in_waiting:
            byte = ser.read(1)
            if byte and byte[0] == FRAME_START_HOST:
                return True
    return False


def read_frame(ser: serial.Serial, timeout: float = 0.1) -> bytes:
    """Read a complete frame starting after START byte"""
    # We already consumed START, read rest of header
    header = ser.read(5)  # TIMESTAMP(4) + N_NODES(1)
    if len(header) < 5:
        return None

    n_nodes = header[4]
    remaining = (n_nodes * 10) + 1  # NODE_DATA + CRC

    data = ser.read(remaining)
    if len(data) < remaining:
        return None

    # Reconstruct full frame
    return bytes([FRAME_START_HOST]) + header + data


def main():
    parser = argparse.ArgumentParser(description='MT6701 Encoder Network Host Receiver')
    parser.add_argument('--port', '-p', default='/dev/ttyUSB0',
                        help='Serial port (default: /dev/ttyUSB0)')
    parser.add_argument('--baud', '-b', type=int, default=115200,
                        help='Baud rate (default: 115200)')
    parser.add_argument('--log', '-l', type=str, default=None,
                        help='Log to CSV file')
    parser.add_argument('--raw', action='store_true',
                        help='Show raw hex bytes')
    parser.add_argument('--quiet', '-q', action='store_true',
                        help='Only show errors and stats')
    args = parser.parse_args()

    # Open serial port
    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
        print(f"Opened {args.port} @ {args.baud} baud")
    except serial.SerialException as e:
        print(f"Error opening {args.port}: {e}")
        sys.exit(1)

    # Open log file if requested
    log_file = None
    if args.log:
        log_file = open(args.log, 'w')
        log_file.write("time,timestamp_us,node_id,angle_rad,angle_deg,velocity,status\n")
        print(f"Logging to {args.log}")

    print("Waiting for frames... (Ctrl+C to stop)\n")

    # Statistics
    frame_count = 0
    error_count = 0
    start_time = time.time()

    try:
        while True:
            # Find frame start
            if not find_frame_start(ser, timeout=1.0):
                continue

            # Read frame
            frame = read_frame(ser)
            if frame is None:
                error_count += 1
                continue

            if args.raw:
                print(f"RAW: {frame.hex()}")

            # Parse frame
            data = parse_host_frame(frame)
            if data is None:
                error_count += 1
                if not args.quiet:
                    print(f"CRC ERROR: {frame.hex()}")
                continue

            frame_count += 1

            # Display
            if not args.quiet:
                # Clear line and print compact output
                line_parts = [f"#{frame_count:6d} t={data['timestamp_us']:10d}us"]
                for node in data['nodes']:
                    line_parts.append(
                        f"N{node['id']}:{node['angle_deg']:6.1f}° "
                        f"{node['velocity']:+6.2f}r/s [{node['status_str']:5s}]"
                    )
                print(" | ".join(line_parts), end='\r')

            # Log to file
            if log_file:
                now = datetime.now().isoformat()
                for node in data['nodes']:
                    log_file.write(
                        f"{now},{data['timestamp_us']},{node['id']},"
                        f"{node['angle_rad']:.6f},{node['angle_deg']:.2f},"
                        f"{node['velocity']:.6f},{node['status']}\n"
                    )
                log_file.flush()

    except KeyboardInterrupt:
        print("\n\n--- Statistics ---")
        elapsed = time.time() - start_time
        print(f"Frames received: {frame_count}")
        print(f"Errors: {error_count}")
        print(f"Duration: {elapsed:.1f}s")
        if elapsed > 0:
            print(f"Frame rate: {frame_count/elapsed:.1f} Hz")

    finally:
        ser.close()
        if log_file:
            log_file.close()
            print(f"Log saved to {args.log}")


if __name__ == '__main__':
    main()