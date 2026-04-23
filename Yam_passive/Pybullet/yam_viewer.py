#!/usr/bin/env python3
"""
YAM Digital Twin Viewer - PyBullet

Real-time joint-state mirror: reads encoder angles from Teensy master stream,
applies them to YAM URDF in PyBullet.

Usage:
    python yam_viewer.py [--port /dev/ttyUSB0]

Ref: Serial_master_node_architecture.md
"""

import argparse
import struct
import sys
import time
import os

try:
    import pybullet as p
    import pybullet_data
except ImportError:
    print("Error: pybullet not installed. Run: pip install pybullet")
    sys.exit(1)

try:
    import serial
except ImportError:
    print("Error: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)

# Protocol constants (from protocol.h)
FRAME_START_HOST = 0xCC
STATUS_VALID = 0x01


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


def parse_host_frame(frame: bytes) -> dict:
    """Parse host stream frame, returns dict or None on error"""
    if len(frame) < 7 or frame[0] != FRAME_START_HOST:
        return None

    timestamp = struct.unpack('<I', frame[1:5])[0]
    n_nodes = frame[5]
    expected_len = 6 + (n_nodes * 10) + 1

    if len(frame) != expected_len:
        return None

    if frame[-1] != crc8(frame[:-1]):
        return None

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
            'velocity': velocity,
            'status': status
        })
        offset += 10

    return {'timestamp_us': timestamp, 'nodes': nodes}


class EncoderStream:
    """Non-blocking serial reader for encoder frames"""

    def __init__(self, port: str, baud: int = 115200):
        self.ser = serial.Serial(port, baud, timeout=0)
        self.buffer = bytearray()

    def read_frame(self) -> dict:
        """Try to read a complete frame, returns None if not ready"""
        # Read available bytes
        if self.ser.in_waiting:
            self.buffer.extend(self.ser.read(self.ser.in_waiting))

        # Look for start byte
        while len(self.buffer) > 0 and self.buffer[0] != FRAME_START_HOST:
            self.buffer.pop(0)

        if len(self.buffer) < 6:
            return None

        # Get expected frame size
        n_nodes = self.buffer[5]
        frame_len = 6 + (n_nodes * 10) + 1

        if len(self.buffer) < frame_len:
            return None

        # Extract and parse frame
        frame = bytes(self.buffer[:frame_len])
        self.buffer = self.buffer[frame_len:]
        return parse_host_frame(frame)

    def close(self):
        self.ser.close()


class YamViewer:
    """PyBullet visualization of YAM arm"""

    def __init__(self, urdf_path: str):
        # Connect to PyBullet GUI
        self.physics_client = p.connect(p.GUI)
        p.setAdditionalSearchPath(pybullet_data.getDataPath())
        p.setGravity(0, 0, -9.81)

        # Load ground plane
        p.loadURDF("plane.urdf")

        # Load YAM (URDF has absolute mesh paths)
        self.robot_id = p.loadURDF(
            urdf_path,
            basePosition=[0, 0, 0],
            baseOrientation=p.getQuaternionFromEuler([0, 0, 0]),
            useFixedBase=True
        )

        # Discover joints
        self.joint_indices = []
        self.joint_names = []

        for j in range(p.getNumJoints(self.robot_id)):
            info = p.getJointInfo(self.robot_id, j)
            joint_name = info[1].decode('utf-8')
            joint_type = info[2]

            if joint_type == p.JOINT_REVOLUTE:
                self.joint_indices.append(j)
                self.joint_names.append(joint_name)

                # Disable default motor
                p.setJointMotorControl2(
                    bodyUniqueId=self.robot_id,
                    jointIndex=j,
                    controlMode=p.VELOCITY_CONTROL,
                    force=0
                )

        print(f"Loaded YAM with {len(self.joint_indices)} joints:")
        for i, name in enumerate(self.joint_names):
            print(f"  [{i}] {name} -> PyBullet index {self.joint_indices[i]}")

        # Camera setup
        p.resetDebugVisualizerCamera(
            cameraDistance=1.0,
            cameraYaw=45,
            cameraPitch=-30,
            cameraTargetPosition=[0, 0, 0.3]
        )

    def set_joint_angles(self, angles: list):
        """Set joint angles (in radians). Maps angles to joints by index."""
        for i, angle in enumerate(angles):
            if i < len(self.joint_indices):
                p.resetJointState(
                    self.robot_id,
                    self.joint_indices[i],
                    float(angle)
                )

    def step(self):
        """Step simulation (for GUI refresh)"""
        p.stepSimulation()


def main():
    parser = argparse.ArgumentParser(description='YAM Digital Twin Viewer')
    parser.add_argument('--port', '-p', default='/dev/ttyUSB0',
                        help='Serial port for encoder stream')
    parser.add_argument('--baud', '-b', type=int, default=115200,
                        help='Baud rate')
    parser.add_argument('--urdf', default=None,
                        help='Path to YAM URDF (default: bundled)')
    parser.add_argument('--demo', action='store_true',
                        help='Demo mode without serial')
    args = parser.parse_args()

    # Find URDF
    if args.urdf:
        urdf_path = args.urdf
    else:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        urdf_path = os.path.join(
            script_dir,
            'i2rt/i2rt/robot_models/arm/yam/yam_pybullet.urdf'
        )

    if not os.path.exists(urdf_path):
        print(f"URDF not found: {urdf_path}")
        sys.exit(1)

    print(f"Using URDF: {urdf_path}")

    # Create viewer
    viewer = YamViewer(urdf_path)

    # Demo mode or serial mode
    if args.demo:
        print("\nDemo mode - use arrow keys or move mouse to close")
        t = 0
        while True:
            # Sinusoidal demo motion
            angles = [
                0.5 * __import__('math').sin(t * 0.5),      # joint1
                0.3 * __import__('math').sin(t * 0.7),      # joint2
                0.4 * __import__('math').sin(t * 0.3),      # joint3
                0.2 * __import__('math').sin(t * 0.9),      # joint4
                0.3 * __import__('math').sin(t * 0.6),      # joint5
                0.5 * __import__('math').sin(t * 0.4),      # joint6
            ]
            viewer.set_joint_angles(angles)
            viewer.step()
            time.sleep(1/60)
            t += 1/60
    else:
        # Serial mode
        try:
            stream = EncoderStream(args.port, args.baud)
            print(f"\nConnected to {args.port} @ {args.baud}")
        except serial.SerialException as e:
            print(f"Error opening {args.port}: {e}")
            sys.exit(1)

        print("Waiting for encoder data to capture zero position...")

        # Joint angle state (match number of URDF joints)
        n_joints = len(viewer.joint_indices)
        joint_angles = [0.0] * n_joints
        zero_offsets = [None] * n_joints  # Captured at startup
        frame_count = 0
        zeroed = False

        try:
            while True:
                # Try to read a frame
                data = stream.read_frame()

                if data:
                    frame_count += 1

                    # Update joint angles from encoder nodes
                    for node in data['nodes']:
                        # Map node ID (1-based) to joint index (0-based)
                        joint_idx = node['id'] - 1
                        if joint_idx < n_joints:
                            raw_angle = node['angle_rad']

                            # Capture zero offset on first valid reading
                            if zero_offsets[joint_idx] is None:
                                zero_offsets[joint_idx] = raw_angle
                                print(f"  Node {node['id']}: zero = {raw_angle:.3f} rad")

                            # Apply relative angle (subtract zero offset)
                            joint_angles[joint_idx] = raw_angle - zero_offsets[joint_idx]

                    # Check if all joints are zeroed
                    if not zeroed and all(o is not None for o in zero_offsets):
                        zeroed = True
                        print(f"\nAll {n_joints} joints zeroed. Move encoders to see motion. (Ctrl+C to stop)\n")

                    # Print status occasionally
                    if frame_count % 100 == 0:
                        angles_str = ' '.join(f'{a:+.2f}' for a in joint_angles)
                        print(f"Frame {frame_count}: delta [{angles_str}]")

                # Update PyBullet
                viewer.set_joint_angles(joint_angles)
                viewer.step()

                # Small sleep to avoid CPU spinning
                time.sleep(1/120)

        except KeyboardInterrupt:
            print(f"\nStopped after {frame_count} frames")
        finally:
            stream.close()


if __name__ == '__main__':
    main()