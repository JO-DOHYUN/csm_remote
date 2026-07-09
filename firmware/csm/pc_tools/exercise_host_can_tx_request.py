import argparse
import time

import serial

from send_host_can_tx_request import build_control_session_frame, build_frame, build_heartbeat_frame, parse_data
from verify_typed_stream import describe, parse_frame, u32


def is_matching_audit(frame: dict, can_id: int, bus: int) -> bool:
    if frame.get("type") != 2:
        return False
    payload = frame.get("payload", b"")
    if len(payload) < 30:
        return False
    return (u32(payload, 8) & 0x1FFFFFFF) == can_id and payload[13] == bus


def is_matching_ack(frame: dict, command_id: int) -> bool:
    if frame.get("type") != 6:
        return False
    payload = frame.get("payload", b"")
    if len(payload) < 28:
        return False
    return u32(payload, 8) == command_id


def main() -> None:
    parser = argparse.ArgumentParser(description="Send HOST_CAN_TX_REQUEST and watch CONTROL_ACK/CAN_TX_RAW.")
    parser.add_argument("--port", default="COM6")
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--command-id", type=lambda s: int(s, 0), default=0x1001)
    parser.add_argument("--bus", type=int, default=0)
    parser.add_argument("--id", type=lambda s: int(s, 0), default=0x503)
    parser.add_argument("--data", default="00 00 00 00 00 00 00 00")
    parser.add_argument("--seconds", type=float, default=4.0)
    args = parser.parse_args()

    frame = build_frame(args.command_id, args.bus, args.id, parse_data(args.data))
    heartbeat_frame = build_heartbeat_frame(args.command_id - 2)
    arm_frame = build_control_session_frame(args.command_id - 1, bus=0xFF, lease_ms=500)
    buf = bytearray()
    sent = False
    saw_ack = False
    saw_audit = False
    send_count = 0
    next_send = time.time() + 0.3
    deadline = time.time() + args.seconds

    with serial.Serial(args.port, args.baud, timeout=0.05) as ser:
        ser.reset_input_buffer()
        ser.write(heartbeat_frame)
        ser.write(arm_frame)
        ser.flush()
        while time.time() < deadline:
            now = time.time()
            if not saw_ack and now >= next_send:
                ser.write(frame)
                ser.flush()
                sent = True
                send_count += 1
                next_send = now + 0.5
                print(
                    f"sent command=0x{args.command_id:08X} bus={args.bus} "
                    f"id=0x{args.id:X} bytes={len(frame)} attempt={send_count}"
                )

            buf += ser.read(4096)
            parsed_this_tick = 0
            while True:
                if time.time() >= deadline:
                    break
                parsed = parse_frame(buf)
                if parsed is None:
                    break
                parsed_this_tick += 1
                if is_matching_ack(parsed, args.command_id):
                    saw_ack = True
                    print(describe(parsed))
                elif parsed.get("type") == 6:
                    print(describe(parsed))
                elif is_matching_audit(parsed, args.id, args.bus):
                    saw_audit = True
                    print(describe(parsed))
                elif parsed.get("type") == 7:
                    print(describe(parsed))
                if saw_ack and saw_audit:
                    print(f"result ack={int(saw_ack)} audit={int(saw_audit)}")
                    raise SystemExit(0)
                if parsed_this_tick >= 256:
                    break

    print(f"result ack={int(saw_ack)} audit={int(saw_audit)}")
    raise SystemExit(0 if saw_ack and saw_audit else 2)


if __name__ == "__main__":
    main()
