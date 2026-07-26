#!/usr/bin/env python3
"""Receive and verify the CSM raw Wi-Fi throughput stream."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import socket
import threading
import time
from pathlib import Path


def verify_pattern(data: bytes, absolute_offset: int) -> int | None:
    for index, value in enumerate(data):
        if value != ((absolute_offset + index) & 0xFF):
            return index
    return None


def parse_board_stat(line: str) -> dict[str, object] | None:
    if not line.startswith(("WIFI_RAW_BOOT ", "WIFI_RAW_STAT ")):
        return None
    result: dict[str, object] = {"kind": line.split(" ", 1)[0]}
    for token in line.strip().split()[1:]:
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        try:
            result[key] = int(value, 0)
        except ValueError:
            result[key] = value
    return result


class SerialCollector:
    def __init__(self, port: str | None, baud: int) -> None:
        self.port = port
        self.baud = baud
        self.lines: list[str] = []
        self.records: list[dict[str, object]] = []
        self.error: str | None = None
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        if self.port is None:
            return
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)

    def _run(self) -> None:
        try:
            import serial

            with serial.Serial(self.port, self.baud, timeout=0.2) as stream:
                while not self._stop.is_set():
                    raw = stream.readline()
                    if not raw:
                        continue
                    line = raw.decode("utf-8", errors="replace").strip()
                    self.lines.append(line)
                    parsed = parse_board_stat(line)
                    if parsed is not None:
                        self.records.append(parsed)
        except Exception as exc:  # hardware diagnostic boundary
            self.error = str(exc)


def run(args: argparse.Namespace) -> dict[str, object]:
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    collector = SerialCollector(args.serial_port, args.serial_baud)
    collector.start()

    intervals: list[dict[str, object]] = []
    digest = hashlib.sha256()
    total_bytes = 0
    mismatch: dict[str, int] | None = None
    unexpected_close = False
    error: str | None = None
    started_wall = time.time()
    started = time.perf_counter()
    interval_started = started
    interval_bytes = 0

    try:
        with socket.create_connection(
            (args.host, args.port), timeout=args.connect_timeout
        ) as stream:
            stream.setsockopt(
                socket.SOL_SOCKET, socket.SO_RCVBUF, args.receive_buffer
            )
            stream.settimeout(0.5)
            deadline = started + args.seconds
            while time.perf_counter() < deadline:
                try:
                    chunk = stream.recv(65536)
                except socket.timeout:
                    chunk = b""
                if chunk:
                    bad_index = verify_pattern(chunk, total_bytes)
                    if bad_index is not None:
                        mismatch = {
                            "absolute_offset": total_bytes + bad_index,
                            "actual": chunk[bad_index],
                            "expected": (total_bytes + bad_index) & 0xFF,
                        }
                        break
                    digest.update(chunk)
                    total_bytes += len(chunk)
                    interval_bytes += len(chunk)
                elif chunk == b"":
                    # A timeout and an orderly close both yield no application
                    # bytes here. MSG_PEEK distinguishes an actual peer close.
                    try:
                        stream.setblocking(False)
                        probe = stream.recv(1, socket.MSG_PEEK)
                        if probe == b"":
                            unexpected_close = True
                            break
                    except BlockingIOError:
                        pass
                    finally:
                        stream.setblocking(True)
                        stream.settimeout(0.5)

                now = time.perf_counter()
                if now - interval_started >= 1.0:
                    elapsed = now - interval_started
                    intervals.append(
                        {
                            "elapsed_s": now - started,
                            "interval_s": elapsed,
                            "bytes": interval_bytes,
                            "bytes_per_s": interval_bytes / elapsed,
                        }
                    )
                    interval_started = now
                    interval_bytes = 0
    except Exception as exc:
        error = str(exc)
    finally:
        collector.stop()

    finished = time.perf_counter()
    duration = finished - started
    rates = [float(item["bytes_per_s"]) for item in intervals]
    summary: dict[str, object] = {
        "gate": "CSM_WIFI_RAW_TCP",
        "pass": (
            error is None
            and mismatch is None
            and not unexpected_close
            and total_bytes > 0
        ),
        "host": args.host,
        "port": args.port,
        "requested_seconds": args.seconds,
        "actual_seconds": duration,
        "started_unix": started_wall,
        "peer": {
            "os": platform.platform(),
            "hostname": socket.gethostname(),
            "receive_buffer_requested": args.receive_buffer,
        },
        "total_bytes": total_bytes,
        "average_bytes_per_s": total_bytes / duration if duration > 0 else 0.0,
        "minimum_interval_bytes_per_s": min(rates) if rates else 0.0,
        "maximum_interval_bytes_per_s": max(rates) if rates else 0.0,
        "sha256": digest.hexdigest(),
        "pattern_mismatch": mismatch,
        "unexpected_close": unexpected_close,
        "error": error,
        "intervals": intervals,
        "board_serial": {
            "port": args.serial_port,
            "error": collector.error,
            "records": collector.records,
            "lines": collector.lines,
        },
    }
    output.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    return summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="192.168.4.1")
    parser.add_argument("--port", type=int, default=3333)
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("--connect-timeout", type=float, default=10.0)
    parser.add_argument("--receive-buffer", type=int, default=1 << 20)
    parser.add_argument("--serial-port")
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    summary = run(args)
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if summary["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
