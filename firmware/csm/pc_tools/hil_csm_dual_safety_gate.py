import argparse
import time

import serial

from send_host_can_tx_request import (
    build_control_session_frame,
    build_frame,
    build_heartbeat_frame,
    parse_data,
)
from verify_typed_stream import describe, parse_frame, u32


CONTROL_ACK = 6
CAPABILITY = 9
BOARD_HEALTH = 8
CAN_RX_RAW = 1
CAN_TX_RAW = 2


class TypedReader:
    def __init__(self, ser):
        self.ser = ser
        self.buf = bytearray()
        self.pending = []
        self.last = None

    def wait_for_frame(self, predicate, timeout_s, label):
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            kept = []
            for frame in self.pending:
                self.last = frame
                if predicate(frame):
                    self.pending = kept + self.pending[len(kept) + 1 :]
                    return frame
                kept.append(frame)
            self.pending = kept

            self.buf += self.ser.read(4096)
            while True:
                frame = parse_frame(self.buf)
                if frame is None:
                    break
                if frame.get("bad_crc"):
                    continue
                self.last = frame
                if predicate(frame):
                    return frame
                self.pending.append(frame)
        raise TimeoutError(
            f"timeout waiting for {label}; last={describe(self.last) if self.last else 'none'}"
        )


def is_ack(command_id, status=None, reason=None):
    def predicate(frame):
        payload = frame.get("payload", b"")
        if frame.get("type") != CONTROL_ACK or len(payload) < 28:
            return False
        if u32(payload, 8) != command_id:
            return False
        if status is not None and payload[12] != status:
            return False
        if reason is not None and payload[13] != reason:
            return False
        return True

    return predicate


def is_reject_ack(command_id):
    def predicate(frame):
        payload = frame.get("payload", b"")
        return (
            frame.get("type") == CONTROL_ACK
            and len(payload) >= 28
            and u32(payload, 8) == command_id
            and payload[12] == 0
        )

    return predicate


def is_tx_audit(bus, can_id, data):
    expected = data[:8] + bytes(max(0, 8 - len(data)))

    def predicate(frame):
        payload = frame.get("payload", b"")
        return (
            frame.get("type") == CAN_TX_RAW
            and len(payload) >= 30
            and payload[13] == bus
            and (u32(payload, 8) & 0x1FFFFFFF) == can_id
            and payload[14:22] == expected
        )

    return predicate


def main():
    parser = argparse.ArgumentParser(description="Run CSM dual-bus safety-gated HIL checks.")
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--id", type=lambda value: int(value, 0), default=0x503)
    parser.add_argument("--timeout", type=float, default=8.0)
    args = parser.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.05) as ser:
        ser.reset_input_buffer()
        reader = TypedReader(ser)

        capability = reader.wait_for_frame(
            lambda frame: frame.get("type") == CAPABILITY and len(frame.get("payload", b"")) >= 112,
            args.timeout,
            "CAPABILITY v3",
        )
        print("PASS capability", describe(capability))

        health = reader.wait_for_frame(
            lambda frame: frame.get("type") == BOARD_HEALTH and len(frame.get("payload", b"")) >= 128,
            args.timeout,
            "BOARD_HEALTH v2",
        )
        print("PASS health", describe(health))

        rx = reader.wait_for_frame(
            lambda frame: frame.get("type") == CAN_RX_RAW and len(frame.get("payload", b"")) >= 30,
            args.timeout,
            "any CAN_RX_RAW",
        )
        print("PASS rx", describe(rx))

        reject_cmd = 0x3001
        ser.write(build_frame(reject_cmd, 0, args.id, parse_data("01 02 03 04 05 06 07 08")))
        ser.flush()
        rejected = reader.wait_for_frame(is_reject_ack(reject_cmd), 3.0, "pre-arm reject")
        print("PASS pre_arm_reject", describe(rejected))

        arm_cmd = 0x3002
        ser.write(build_heartbeat_frame(arm_cmd - 1))
        ser.write(build_control_session_frame(arm_cmd, bus=0xFF, lease_ms=500))
        ser.flush()
        armed = reader.wait_for_frame(is_ack(arm_cmd, status=1, reason=0), 3.0, "arm ack")
        print("PASS arm", describe(armed))

        for bus, data_text, command_id in (
            (0, "10 11 12 13 14 15 16 17", 0x3010),
            (1, "20 21 22 23 24 25 26 27", 0x3020),
        ):
            data = parse_data(data_text)
            ser.write(build_heartbeat_frame(command_id - 1))
            ser.write(build_frame(command_id, bus, args.id, data))
            ser.flush()
            ack = reader.wait_for_frame(is_ack(command_id, status=1, reason=0), 3.0, f"bus{bus} ack")
            audit = reader.wait_for_frame(is_tx_audit(bus, args.id, data), 3.0, f"bus{bus} audit")
            print(f"PASS bus{bus}_ack", describe(ack))
            print(f"PASS bus{bus}_audit", describe(audit))

        time.sleep(0.7)
        timeout_cmd = 0x3030
        ser.write(build_frame(timeout_cmd, 0, args.id, parse_data("31 32 33 34 35 36 37 38")))
        ser.flush()
        timeout_reject = reader.wait_for_frame(is_reject_ack(timeout_cmd), 3.0, "timeout reject")
        reason = timeout_reject["payload"][13]
        if reason not in (10, 11):
            raise RuntimeError(f"expected timeout/lease reject reason 10 or 11, got {reason}")
        print("PASS timeout_reject", describe(timeout_reject))

    print("RESULT PASS")


if __name__ == "__main__":
    main()
