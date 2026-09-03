#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import socket
import struct
import time
from datetime import datetime

from typed_frame import build_typed_frame
from verify_typed_stream import GapTracker, describe, i16, i32, parse_frame, u32, u64


HOST_QUERY_CAPABILITY = 14
DEFAULT_VISIBLE_TYPES = {6, 7, 8, 9, 17}
RATE_LIMITED_DEBUG_TYPES = {2, 18, 19}


def compact_describe(frame: dict) -> str:
    record_type = frame["type"]
    payload = frame["payload"]
    seq = frame["seq"]
    if record_type == 8 and len(payload) >= 508:
        return (
            f"[HEALTH] seq={seq} boot=0x{u64(payload, 304):016X}/{u32(payload, 416)} "
            f"can_rx={u32(payload, 8)} drop={u32(payload, 12)} fifo={u32(payload, 16)} "
            f"can_tx={u32(payload, 88)} tx_fail={u32(payload, 92)} "
            f"wifi_epoch={u32(payload, 328)} connect={u32(payload, 344)} "
            f"disconnect={u32(payload, 348)} sent={u32(payload, 340)} "
            f"overflow={u32(payload, 336)} stall={u32(payload, 352)} "
            f"socket_error={u32(payload, 360)} main_gap_us={u32(payload, 380)} "
            f"fault=0x{u32(payload, 48):08X} source=0x{u32(payload, 412):08X}"
        )
    if record_type == 18 and len(payload) >= 232:
        return (
            f"[RC] seq={seq} schema={payload[8]} link={payload[9]} flags=0x{payload[12]:02X} "
            f"age_ms={u32(payload, 24)} steer={i16(payload, 30)} "
            f"ch5={i16(payload, 136)} ch10={i16(payload, 146)} ch11={i16(payload, 148)} "
            f"valid={u32(payload, 44)} malformed={u32(payload, 96)} "
            f"bad_addr={u32(payload, 92)} bad_len={u32(payload, 56)} "
            f"bad_crc={u32(payload, 60)} qualified={payload[119]} "
            f"streak={payload[118]} admission_reason={payload[116] | payload[117] << 8} "
            f"channel_mask=0x{payload[114] | payload[115] << 8:04X} "
            f"budget_hits={u32(payload, 228)}"
        )
    if record_type == 19 and len(payload) >= 128:
        return (
            f"[RUNTIME] seq={seq} phase={payload[9]} boot_phase={payload[10]} "
            f"wifi_phase={payload[72]} flags=0x{payload[73]:02X} "
            f"call_seq={u32(payload, 76)} duration_us={u32(payload, 84)} "
            f"result={i32(payload, 88)} heartbeat_age_ms={u32(payload, 92)} "
            f"stall={u32(payload, 96)} epoch={u32(payload, 100)} "
            f"boot=0x{u64(payload, 104):016X}"
        )
    return describe(frame)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Monitor the CSM canonical typed stream over Wi-Fi TCP."
    )
    parser.add_argument("--host", default="192.168.4.1")
    parser.add_argument("--port", type=int, default=3333)
    parser.add_argument(
        "--seconds", type=float, default=0.0,
        help="Stop after this many seconds; 0 reconnects and runs until Ctrl+C.",
    )
    parser.add_argument("--reconnect-ms", type=int, default=1000)
    parser.add_argument("--raw", action="store_true", help="Print every typed record.")
    parser.add_argument(
        "--debug-interval", type=float, default=1.0,
        help="Minimum print interval per high-rate debug record type.",
    )
    parser.add_argument(
        "--query-capability", action="store_true",
        help="Send one non-control HOST_QUERY_CAPABILITY after each connection.",
    )
    parser.add_argument(
        "--send-hex", default="",
        help="Send one complete pre-encoded typed frame as hexadecimal after connect.",
    )
    parser.add_argument("--output-dir", default="")
    parser.add_argument(
        "--raw-max-mib", type=float, default=256.0,
        help="Maximum raw capture size; 0 disables the cap.",
    )
    return parser.parse_args()


def output_directory(value: str) -> pathlib.Path:
    if value:
        path = pathlib.Path(value)
    else:
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        path = pathlib.Path(__file__).resolve().parents[1] / "artifacts" / "tcp_monitor" / stamp
    path.mkdir(parents=True, exist_ok=True)
    return path


def send_initial_frames(client: socket.socket, args: argparse.Namespace, epoch: int) -> None:
    if args.query_capability:
        command_id = (0xC5000000 | (epoch & 0x00FFFFFF)) & 0xFFFFFFFF
        payload = struct.pack("<I", command_id)
        client.sendall(build_typed_frame(HOST_QUERY_CAPABILITY, command_id, payload))
        print(f"[TX] HOST_QUERY_CAPABILITY command=0x{command_id:08X}", flush=True)
    if args.send_hex:
        encoded = bytes.fromhex(args.send_hex)
        client.sendall(encoded)
        print(f"[TX] pre-encoded bytes={len(encoded)}", flush=True)


def main() -> int:
    args = parse_args()
    output = output_directory(args.output_dir)
    raw_path = output / "csm-tcp.raw"
    tracker = GapTracker()
    buffer = bytearray()
    deadline = time.monotonic() + args.seconds if args.seconds > 0 else None
    epoch = 0
    frames = 0
    bad_crc = 0
    received_bytes = 0
    raw_written = raw_path.stat().st_size if raw_path.exists() else 0
    raw_limit = int(args.raw_max_mib * 1024 * 1024) if args.raw_max_mib > 0 else 0
    raw_capped = raw_limit > 0 and raw_written >= raw_limit
    last_debug_print: dict[int, float] = {}

    print(f"[START] endpoint={args.host}:{args.port} raw={raw_path}", flush=True)
    try:
        with raw_path.open("ab") as raw_file:
            while deadline is None or time.monotonic() < deadline:
                try:
                    with socket.create_connection((args.host, args.port), timeout=5) as client:
                        epoch += 1
                        buffer.clear()
                        client.settimeout(0.5)
                        print(f"[CONNECTED] client_epoch={epoch}", flush=True)
                        send_initial_frames(client, args, epoch)
                        while deadline is None or time.monotonic() < deadline:
                            try:
                                chunk = client.recv(65536)
                            except socket.timeout:
                                continue
                            if not chunk:
                                raise ConnectionError("CSM closed TCP connection")
                            if not raw_capped:
                                writable = len(chunk)
                                if raw_limit > 0:
                                    writable = min(writable, raw_limit - raw_written)
                                if writable > 0:
                                    raw_file.write(chunk[:writable])
                                    raw_file.flush()
                                    raw_written += writable
                                if raw_limit > 0 and raw_written >= raw_limit:
                                    raw_capped = True
                                    print(f"[RAW_CAP] bytes={raw_written}", flush=True)
                            received_bytes += len(chunk)
                            buffer.extend(chunk)
                            while True:
                                frame = parse_frame(buffer)
                                if frame is None:
                                    break
                                if frame.get("bad_crc"):
                                    bad_crc += 1
                                    print(describe(frame), flush=True)
                                    continue
                                frames += 1
                                tracker.observe(frame)
                                record_type = frame["type"]
                                now = time.monotonic()
                                debug_due = (
                                    record_type in RATE_LIMITED_DEBUG_TYPES
                                    and now - last_debug_print.get(record_type, 0.0)
                                    >= max(0.0, args.debug_interval)
                                )
                                if args.raw or record_type in DEFAULT_VISIBLE_TYPES or debug_due:
                                    formatter = describe if args.raw else compact_describe
                                    print(formatter(frame), flush=True)
                                    if record_type in RATE_LIMITED_DEBUG_TYPES:
                                        last_debug_print[record_type] = now
                except (ConnectionError, OSError) as exc:
                    print(f"[DISCONNECTED] client_epoch={epoch} reason={exc}", flush=True)
                    if deadline is not None and time.monotonic() >= deadline:
                        break
                    time.sleep(max(0, args.reconnect_ms) / 1000.0)
    except KeyboardInterrupt:
        print("[STOP] Ctrl+C", flush=True)

    print(
        f"[DONE] epochs={epoch} bytes={received_bytes} raw_bytes={raw_written} "
        f"frames={frames} bad_crc={bad_crc}",
        flush=True,
    )
    print(tracker.summary(), flush=True)
    print(f"artifact={output}", flush=True)
    return 0 if bad_crc == 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
