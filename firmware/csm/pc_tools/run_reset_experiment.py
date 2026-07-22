#!/usr/bin/env python3
"""Run one USB-only CSM reset-isolation experiment and save its evidence.

This runner is intentionally independent of PlatformIO build state.  It consumes
the canonical typed stream, reconnects when Windows removes/re-enumerates the
CDC port, and decides PASS/FAIL only from BOARD_HEALTH v13 evidence collected
during this invocation.
"""

import argparse
import datetime
import json
import sys
import time
from collections import Counter
from pathlib import Path

import serial

from verify_typed_stream import i32, parse_frame, u32, u64


BOARD_HEALTH_TYPE = 8
RUNTIME_DIAGNOSTIC_TYPE = 19
BOARD_HEALTH_V13_SIZE = 508
RUNTIME_DIAGNOSTIC_SIZE = 128
SERIAL_READ_TIMEOUT_S = 0.20
SERIAL_RETRY_S = 0.25
SILENCE_REOPEN_S = 10.0
FINAL_HEALTH_MAX_AGE_S = 3.0
MAX_HEALTH_GAP_S = 5.0

PROFILE_EXPECTATIONS = {
    0: {"name": "REF", "requested_wifi_mode": 2, "effective_wifi_mode": 2,
        "watchdog_requested": True, "watchdog_effective": True, "experiment_flags": 0x7F},
    1: {"name": "A", "requested_wifi_mode": 0, "effective_wifi_mode": 0,
        "watchdog_requested": True, "watchdog_effective": True, "experiment_flags": 0x7F},
    2: {"name": "B", "requested_wifi_mode": 2, "effective_wifi_mode": 2,
        "watchdog_requested": False, "watchdog_effective": False, "experiment_flags": 0x06},
    3: {"name": "C", "requested_wifi_mode": 1, "effective_wifi_mode": 1,
        "watchdog_requested": True, "watchdog_effective": True, "experiment_flags": 0x7F},
}

MAX_FIELDS = (
    "builtin_can_tx_success",
    "builtin_can_tx_fail",
    "consecutive_early_resets",
    "early_reset_total",
    "wifi_quarantine_total",
    "retained_corrupt_metadata",
    "retained_corrupt_events",
    "usb_overflow",
    "wifi_overflow",
    "wifi_connect",
    "wifi_disconnect",
    "wifi_stall_close",
    "no_sink_drop",
    "wifi_socket_error",
    "wifi_send_budget_overrun",
    "wifi_send_call_max_us",
    "wifi_recv_call_max_us",
    "wifi_close_call_max_us",
    "main_loop_max_gap_us",
)

DELTA_FIELDS = (
    "board_mono_us",
    "current_progress_uptime_ms",
    "builtin_can_tx_success",
    "builtin_can_tx_fail",
    "usb_sent",
    "usb_overflow",
    "wifi_sent",
    "wifi_overflow",
    "wifi_connect",
    "wifi_disconnect",
    "wifi_stall_close",
    "wifi_socket_error",
    "wifi_send_budget_overrun",
)


def source_id_arg(value: str) -> int:
    text = value.strip().replace("_", "")
    try:
        parsed = int(text, 16)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("source id must be a 32-bit hexadecimal value") from exc
    if not 0 <= parsed <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("source id must fit in 32 bits")
    return parsed


def utc_now() -> str:
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")


def hex32(value: int) -> str:
    return f"0x{value:08X}"


def hex64(value: int) -> str:
    return f"0x{value:016X}"


def health_snapshot(payload: bytes, host_elapsed_s: float) -> dict:
    recovery_flags = u32(payload, 408)
    profile_word = u32(payload, 464)
    integrity_word = u32(payload, 468)
    experiment_flags = (profile_word >> 24) & 0xFF
    return {
        "host_elapsed_s": round(host_elapsed_s, 3),
        "board_mono_us": u64(payload, 0),
        "health_version": payload[52],
        "builtin_can_tx_success": u32(payload, 88),
        "builtin_can_tx_fail": u32(payload, 92),
        "boot_session": u64(payload, 304),
        "usb_epoch": u32(payload, 312),
        "usb_overflow": u32(payload, 320),
        "usb_sent": u32(payload, 324),
        "wifi_epoch": u32(payload, 328),
        "wifi_overflow": u32(payload, 336),
        "wifi_sent": u32(payload, 340),
        "wifi_connect": u32(payload, 344),
        "wifi_disconnect": u32(payload, 348),
        "wifi_stall_close": u32(payload, 352),
        "no_sink_drop": u32(payload, 356),
        "wifi_socket_error": u32(payload, 360),
        "wifi_send_budget_overrun": u32(payload, 364),
        "wifi_send_call_max_us": u32(payload, 368),
        "wifi_recv_call_max_us": u32(payload, 372),
        "wifi_close_call_max_us": u32(payload, 376),
        "main_loop_max_gap_us": u32(payload, 380),
        "reset_cause_bits": u32(payload, 384),
        "reset_status_raw": u32(payload, 388),
        "recovery_flags": recovery_flags,
        "recovery_ready": bool(recovery_flags & (1 << 0)),
        "previous_boot_valid": bool(recovery_flags & (1 << 1)),
        "previous_boot_stable": bool(recovery_flags & (1 << 2)),
        "current_boot_stable": bool(recovery_flags & (1 << 3)),
        "wifi_quarantined": bool(recovery_flags & (1 << 4)),
        "wifi_start_allowed": bool(recovery_flags & (1 << 5)),
        "firmware_source_id32": u32(payload, 412),
        "boot_sequence": u32(payload, 416),
        "consecutive_early_resets": u32(payload, 420),
        "early_reset_total": u32(payload, 424),
        "wifi_quarantine_total": u32(payload, 428),
        "previous_boot_sequence": u32(payload, 432),
        "previous_progress_id": u32(payload, 436),
        "previous_progress_detail": u32(payload, 440),
        "previous_progress_uptime_ms": u32(payload, 444),
        "current_progress_id": u32(payload, 448),
        "current_progress_detail": u32(payload, 452),
        "current_progress_uptime_ms": u32(payload, 456),
        "retained_event_sequence": u32(payload, 460),
        "experiment_selector": profile_word & 0xFF,
        "requested_wifi_mode": (profile_word >> 8) & 0xFF,
        "effective_wifi_mode": (profile_word >> 16) & 0xFF,
        "experiment_flags": experiment_flags,
        "watchdog_effective": bool(experiment_flags & (1 << 0)),
        "watchdog_requested": bool(experiment_flags & (1 << 3)),
        "watchdog_start_called": bool(experiment_flags & (1 << 4)),
        "watchdog_start_succeeded": bool(experiment_flags & (1 << 5)),
        "watchdog_timeout_matches": bool(experiment_flags & (1 << 6)),
        "retained_valid_events": integrity_word & 0xFFFF,
        "retained_corrupt_metadata": (integrity_word >> 16) & 0xFF,
        "retained_corrupt_events": (integrity_word >> 24) & 0xFF,
        "runtime_contract_id32": u32(payload, 472),
        "recovery_identity_id32": u32(payload, 476),
        "watchdog_observed_timeout_ms": u32(payload, 480),
        "previous_wifi_call_flags": u32(payload, 484),
        "previous_wifi_call_valid": bool(u32(payload, 484) & (1 << 0)),
        "previous_wifi_call_in_progress": bool(u32(payload, 484) & (1 << 1)),
        "previous_wifi_call_completed": bool(u32(payload, 484) & (1 << 2)),
        "previous_wifi_call_contract_changed": bool(u32(payload, 484) & (1 << 3)),
        "previous_wifi_call_owner": (u32(payload, 484) >> 8) & 0xFF,
        "previous_wifi_call_operation": (u32(payload, 484) >> 16) & 0xFF,
        "previous_wifi_call_boot_sequence": u32(payload, 488),
        "previous_wifi_call_sequence": u32(payload, 492),
        "previous_wifi_call_started_ms": u32(payload, 496),
        "previous_wifi_call_duration_us": u32(payload, 500),
        "previous_wifi_call_result": i32(payload, 504),
    }


def json_snapshot(snapshot: dict) -> dict:
    result = dict(snapshot)
    result["boot_session"] = hex64(snapshot["boot_session"])
    result["firmware_source_id32"] = hex32(snapshot["firmware_source_id32"])
    result["runtime_contract_id32"] = hex32(snapshot["runtime_contract_id32"])
    result["recovery_identity_id32"] = hex32(snapshot["recovery_identity_id32"])
    result["reset_cause_bits"] = hex32(snapshot["reset_cause_bits"])
    result["reset_status_raw"] = hex32(snapshot["reset_status_raw"])
    result["recovery_flags"] = hex32(snapshot["recovery_flags"])
    result["experiment_flags"] = f"0x{snapshot['experiment_flags']:02X}"
    return result


def add_exact_set_failure(failures: list, label: str, observed: set, expected: int, formatter=str) -> None:
    if observed != {expected}:
        rendered = [formatter(value) for value in sorted(observed)]
        failures.append(f"{label}: expected {formatter(expected)}, observed {rendered}")


def evaluate(args, expectation, state, elapsed_s, interrupted) -> dict:
    failures = []
    health = state["health"]
    if interrupted:
        failures.append("experiment interrupted before its requested duration")
    if elapsed_s + 0.25 < args.seconds:
        failures.append(f"elapsed time {elapsed_s:.2f}s is shorter than requested {args.seconds:.2f}s")
    if state["crc_errors"]:
        failures.append(f"typed stream CRC errors: {state['crc_errors']}")
    if state["sequence_gap_events"]:
        failures.append(
            f"typed sequence gaps after stream establishment: events={state['sequence_gap_events']} "
            f"missing={state['sequence_missing_records']}"
        )
    failures.extend(state["runtime_diagnostic_errors"])
    if len(health) < 2:
        failures.append(f"BOARD_HEALTH v13 samples: expected at least 2, observed {len(health)}")

    if health:
        sources = {item["firmware_source_id32"] for item in health}
        sessions = {item["boot_session"] for item in health}
        boot_sequences = {item["boot_sequence"] for item in health}
        selectors = {item["experiment_selector"] for item in health}
        requested_modes = {item["requested_wifi_mode"] for item in health}
        effective_modes = {item["effective_wifi_mode"] for item in health}
        experiment_flags = {item["experiment_flags"] for item in health}

        add_exact_set_failure(failures, "firmware source id", sources, args.expect_source_id, hex32)
        if len(sessions) != 1 or 0 in sessions:
            failures.append(f"boot session changed or is invalid: {[hex64(value) for value in sorted(sessions)]}")
        if len(boot_sequences) != 1 or 0 in boot_sequences:
            failures.append(f"boot sequence changed or is invalid: {sorted(boot_sequences)}")
        add_exact_set_failure(failures, "experiment selector", selectors, args.expect_selector)
        add_exact_set_failure(
            failures, "requested Wi-Fi mode", requested_modes, expectation["requested_wifi_mode"]
        )
        add_exact_set_failure(
            failures, "effective Wi-Fi mode", effective_modes, expectation["effective_wifi_mode"]
        )
        add_exact_set_failure(
            failures,
            "experiment flags",
            experiment_flags,
            expectation["experiment_flags"],
            lambda value: f"0x{value:02X}",
        )
        expected_watchdog_timeout_ms = 3000 if expectation["watchdog_effective"] else 0
        add_exact_set_failure(
            failures,
            "observed watchdog timeout",
            {item["watchdog_observed_timeout_ms"] for item in health},
            expected_watchdog_timeout_ms,
        )
        if any(item["runtime_contract_id32"] == 0 for item in health):
            failures.append("runtime contract identity is zero")
        if any(item["recovery_identity_id32"] == 0 for item in health):
            failures.append("composed recovery identity is zero")

        watchdog_requested = {item["watchdog_requested"] for item in health}
        watchdog_effective = {item["watchdog_effective"] for item in health}
        add_exact_set_failure(
            failures, "watchdog requested state", watchdog_requested,
            expectation["watchdog_requested"], lambda value: str(bool(value)).lower()
        )
        add_exact_set_failure(
            failures, "watchdog effective state", watchdog_effective,
            expectation["watchdog_effective"], lambda value: str(bool(value)).lower()
        )
        if expectation["watchdog_requested"]:
            if not all(item["watchdog_start_called"] for item in health):
                failures.append("watchdog was requested but start() was not called")
            if not all(item["watchdog_start_succeeded"] for item in health):
                failures.append("watchdog start() did not report success")
            if not all(item["watchdog_timeout_matches"] for item in health):
                failures.append("watchdog get_timeout() did not match the requested timeout")
        else:
            if any(item["watchdog_start_called"] for item in health):
                failures.append("profile B called watchdog start() despite requesting watchdog off")
            if any(item["watchdog_start_succeeded"] for item in health):
                failures.append("profile B reported an impossible watchdog start() success")
            if any(item["watchdog_timeout_matches"] for item in health):
                failures.append("profile B reported a watchdog timeout match while watchdog was requested off")

        if not all(item["recovery_ready"] for item in health):
            failures.append("BootRecovery was not ready in every BOARD_HEALTH sample")
        if any(item["wifi_quarantined"] for item in health):
            failures.append("Wi-Fi quarantine became active during the experiment")
        if health[-1]["consecutive_early_resets"] != 0:
            failures.append(
                f"final consecutive early-reset count is {health[-1]['consecutive_early_resets']}, expected 0"
            )
        if not health[-1]["current_boot_stable"]:
            failures.append("final BOARD_HEALTH did not reach the retained 30s stable mark")
        if max(item["retained_corrupt_metadata"] for item in health) != 0:
            failures.append("retained metadata corruption was reported")
        if max(item["retained_corrupt_events"] for item in health) != 0:
            failures.append("retained event corruption was reported")
        if max(item["builtin_can_tx_success"] for item in health) != 0:
            failures.append("built-in application CAN TX success counter is nonzero in a TX-suppressed profile")
        if max(item["builtin_can_tx_fail"] for item in health) != 0:
            failures.append("built-in application CAN TX failure counter is nonzero in a TX-suppressed profile")

        progress_delta_ms = health[-1]["current_progress_uptime_ms"] - health[0]["current_progress_uptime_ms"]
        mono_delta_us = health[-1]["board_mono_us"] - health[0]["board_mono_us"]
        if progress_delta_ms <= 0:
            failures.append("current retained progress uptime did not advance")
        if mono_delta_us <= 0:
            failures.append("board monotonic time did not advance")

        final_health_age_s = max(0.0, elapsed_s - state["last_health_elapsed_s"])
        if final_health_age_s > FINAL_HEALTH_MAX_AGE_S:
            failures.append(f"final BOARD_HEALTH is stale by {final_health_age_s:.2f}s")
        if state["max_health_gap_s"] > MAX_HEALTH_GAP_S:
            failures.append(f"BOARD_HEALTH gap reached {state['max_health_gap_s']:.2f}s")
    else:
        final_health_age_s = None

    first = health[0] if health else None
    last = health[-1] if health else None
    maxima = {field: max(item[field] for item in health) for field in MAX_FIELDS} if health else {}
    deltas = {field: last[field] - first[field] for field in DELTA_FIELDS} if health else {}

    source_ids = sorted({item["firmware_source_id32"] for item in health})
    boot_sessions = sorted({item["boot_session"] for item in health})
    result = {
        "schema_version": 1,
        "status": "PASS" if not failures else "FAIL",
        "started_utc": state["started_utc"],
        "finished_utc": utc_now(),
        "test": {
            "profile": expectation["name"],
            "port": args.port,
            "baud": args.baud,
            "requested_seconds": args.seconds,
            "elapsed_seconds": round(elapsed_s, 3),
        },
        "expectation": {
            "selector": args.expect_selector,
            "source_id32": hex32(args.expect_source_id),
            "requested_wifi_mode": expectation["requested_wifi_mode"],
            "effective_wifi_mode": expectation["effective_wifi_mode"],
            "experiment_flags": f"0x{expectation['experiment_flags']:02X}",
            "watchdog_requested": expectation["watchdog_requested"],
            "watchdog_effective": expectation["watchdog_effective"],
            "application_data_can_tx_suppressed": True,
        },
        "serial": {
            "connection_opens": state["connection_opens"],
            "open_failures": state["open_failures"],
            "disconnect_events": state["disconnect_events"],
            "silence_reopens": state["silence_reopens"],
        },
        "typed_stream": {
            "valid_records": state["valid_records"],
            "record_counts": {str(key): value for key, value in sorted(state["record_counts"].items())},
            "crc_errors": state["crc_errors"],
            "sequence_gap_events": state["sequence_gap_events"],
            "sequence_missing_records": state["sequence_missing_records"],
        },
        "runtime_diagnostic": {
            "phase7_recovery_event_count": len(state["recovery_events"]),
            "phase7_event_types": dict(sorted(state["recovery_event_types"].items())),
            "phase7_events": state["recovery_events"],
            "phase8_recovered_wifi_call_count": len(state["recovered_wifi_calls"]),
            "phase8_recovered_wifi_calls": state["recovered_wifi_calls"],
            "phase8_in_progress_count": sum(
                1 for item in state["recovered_wifi_calls"] if item["in_progress"]
            ),
            "contract_errors": state["runtime_diagnostic_errors"],
            "phase8_required": False,
            "phase8_absence_note": (
                "absence is valid when retained call-latch storage was fresh or contained no preceding call"
            ),
        },
        "board_health": {
            "v13_samples": len(health),
            "older_or_short_samples": state["short_health_records"],
            "time_to_first_sample_s": state["first_health_elapsed_s"],
            "final_sample_age_s": round(final_health_age_s, 3) if final_health_age_s is not None else None,
            "max_inter_sample_gap_s": round(state["max_health_gap_s"], 3),
            "source_ids": [hex32(value) for value in source_ids],
            "boot_sessions": [hex64(value) for value in boot_sessions],
            "boot_sequences": sorted({item["boot_sequence"] for item in health}),
            "selectors": sorted({item["experiment_selector"] for item in health}),
            "requested_wifi_modes": sorted({item["requested_wifi_mode"] for item in health}),
            "effective_wifi_modes": sorted({item["effective_wifi_mode"] for item in health}),
            "experiment_flags": [
                f"0x{value:02X}" for value in sorted({item["experiment_flags"] for item in health})
            ],
            "stable_observed": any(item["current_boot_stable"] for item in health),
            "maxima": maxima,
            "deltas": deltas,
            "first": json_snapshot(first) if first else None,
            "last": json_snapshot(last) if last else None,
        },
        "failures": failures,
    }
    return result


def write_result(path: Path, result: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as output:
        json.dump(result, output, ensure_ascii=False, indent=2, sort_keys=True)
        output.write("\n")
    temporary.replace(path)


def open_serial(args):
    port = serial.Serial(args.port, args.baud, timeout=SERIAL_READ_TIMEOUT_S)
    port.reset_input_buffer()
    return port


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run a reconnecting USB BOARD_HEALTH v13 reset experiment and emit JSON evidence."
    )
    parser.add_argument("--port", required=True, help="USB CDC port, for example COM7")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seconds", type=float, default=180.0)
    parser.add_argument("--expect-selector", type=int, choices=sorted(PROFILE_EXPECTATIONS), required=True)
    parser.add_argument(
        "--expect-source-id",
        type=source_id_arg,
        required=True,
        help="Expected 32-bit source-manifest id in hex, for example F9057D3F",
    )
    parser.add_argument("--output", type=Path, required=True, help="JSON evidence output path")
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be greater than zero")

    expectation = PROFILE_EXPECTATIONS[args.expect_selector]
    started = time.monotonic()
    deadline = started + args.seconds
    state = {
        "started_utc": utc_now(),
        "connection_opens": 0,
        "open_failures": 0,
        "disconnect_events": 0,
        "silence_reopens": 0,
        "valid_records": 0,
        "record_counts": Counter(),
        "crc_errors": 0,
        "sequence_gap_events": 0,
        "sequence_missing_records": 0,
        "runtime_diagnostic_errors": [],
        "recovery_events": [],
        "recovery_event_types": Counter(),
        "recovered_wifi_calls": [],
        "short_health_records": 0,
        "health": [],
        "first_health_elapsed_s": None,
        "last_health_elapsed_s": 0.0,
        "max_health_gap_s": 0.0,
    }
    port = None
    buffer = bytearray()
    last_sequence = None
    last_byte_at = started
    next_open_attempt = started
    next_open_log = started
    next_status = started + 30.0
    interrupted = False

    print(
        f"[START] profile={expectation['name']} selector={args.expect_selector} "
        f"source={hex32(args.expect_source_id)} port={args.port} duration={args.seconds:.1f}s",
        flush=True,
    )

    try:
        while time.monotonic() < deadline:
            now = time.monotonic()
            if port is None:
                if now < next_open_attempt:
                    time.sleep(min(SERIAL_RETRY_S, next_open_attempt - now))
                    continue
                try:
                    port = open_serial(args)
                    state["connection_opens"] += 1
                    buffer.clear()
                    last_sequence = None
                    last_byte_at = time.monotonic()
                    print(f"[USB] connected epoch={state['connection_opens']} port={args.port}", flush=True)
                except (serial.SerialException, OSError) as exc:
                    state["open_failures"] += 1
                    if now >= next_open_log:
                        print(f"[USB] waiting for {args.port}: {exc}", flush=True)
                        next_open_log = now + 5.0
                    next_open_attempt = now + SERIAL_RETRY_S
                    continue

            try:
                waiting = port.in_waiting
                chunk = port.read(min(max(waiting, 1), 4096))
            except (serial.SerialException, OSError) as exc:
                state["disconnect_events"] += 1
                print(f"[USB] disconnected: {exc}", flush=True)
                try:
                    port.close()
                except (serial.SerialException, OSError):
                    pass
                port = None
                next_open_attempt = time.monotonic() + SERIAL_RETRY_S
                continue

            now = time.monotonic()
            if chunk:
                last_byte_at = now
                buffer.extend(chunk)
                while True:
                    frame = parse_frame(buffer)
                    if frame is None:
                        break
                    if frame.get("bad_crc"):
                        state["crc_errors"] += 1
                        continue

                    state["valid_records"] += 1
                    state["record_counts"][frame["type"]] += 1
                    sequence = frame["seq"]
                    if last_sequence is not None:
                        expected_sequence = (last_sequence + 1) & 0xFFFF
                        if sequence != expected_sequence:
                            state["sequence_gap_events"] += 1
                            state["sequence_missing_records"] += (sequence - expected_sequence) & 0xFFFF
                    last_sequence = sequence

                    if frame["type"] == RUNTIME_DIAGNOSTIC_TYPE:
                        payload = frame["payload"]
                        if len(payload) >= RUNTIME_DIAGNOSTIC_SIZE:
                            phase = payload[9]
                            if phase == 7:
                                event_type = payload[10]
                                state["recovery_event_types"][str(event_type)] += 1
                                state["recovery_events"].append({
                                    "event_type": event_type,
                                    "event_sequence": u32(payload, 12),
                                    "boot_sequence": u32(payload, 16),
                                    "uptime_ms": u32(payload, 20),
                                    "value": u32(payload, 24),
                                    "code": u32(payload, 28),
                                    "firmware_identity_tag": hex32(u32(payload, 32)),
                                    "boot_session": hex64(u64(payload, 104)),
                                })
                                if not (payload[11] & (1 << 6)):
                                    state["runtime_diagnostic_errors"].append(
                                        "phase 7 recovery event lacks the recovered flag"
                                    )
                            elif phase == 8:
                                call_flags = payload[73]
                                in_progress = bool(call_flags & (1 << 0))
                                completed = bool(call_flags & (1 << 6))
                                state["recovered_wifi_calls"].append({
                                    "owner": payload[10],
                                    "operation": payload[72],
                                    "call_sequence": u32(payload, 76),
                                    "started_uptime_ms": u32(payload, 80),
                                    "duration_us": u32(payload, 84),
                                    "result": i32(payload, 88),
                                    "previous_boot_sequence": u32(payload, 16),
                                    "completed_uptime_ms": u32(payload, 20),
                                    "runtime_contract_id": hex64(u64(payload, 32)),
                                    "boot_session": hex64(u64(payload, 104)),
                                    "in_progress": in_progress,
                                    "completed": completed,
                                    "contract_changed": bool(call_flags & (1 << 7)),
                                })
                                if not (payload[11] & (1 << 6)) or not (call_flags & (1 << 5)):
                                    state["runtime_diagnostic_errors"].append(
                                        "phase 8 Wi-Fi call lacks recovered evidence flags"
                                    )
                                if in_progress and completed:
                                    state["runtime_diagnostic_errors"].append(
                                        "phase 8 Wi-Fi call is both in-progress and completed"
                                    )

                    if frame["type"] != BOARD_HEALTH_TYPE:
                        continue
                    payload = frame["payload"]
                    if len(payload) < BOARD_HEALTH_V13_SIZE or payload[52] < 13:
                        state["short_health_records"] += 1
                        continue

                    elapsed = now - started
                    snapshot = health_snapshot(payload, elapsed)
                    if state["health"]:
                        gap = elapsed - state["last_health_elapsed_s"]
                        state["max_health_gap_s"] = max(state["max_health_gap_s"], gap)
                    else:
                        state["first_health_elapsed_s"] = round(elapsed, 3)
                    state["last_health_elapsed_s"] = elapsed
                    state["health"].append(snapshot)
            elif now - last_byte_at >= SILENCE_REOPEN_S:
                state["disconnect_events"] += 1
                state["silence_reopens"] += 1
                print(f"[USB] no bytes for {SILENCE_REOPEN_S:.0f}s; reopening {args.port}", flush=True)
                try:
                    port.close()
                except (serial.SerialException, OSError):
                    pass
                port = None
                next_open_attempt = now + SERIAL_RETRY_S

            if now >= next_status:
                latest = state["health"][-1] if state["health"] else None
                latest_text = (
                    f" boot={latest['boot_sequence']} stable={int(latest['current_boot_stable'])} "
                    f"progress_ms={latest['current_progress_uptime_ms']}"
                    if latest
                    else " no-health"
                )
                print(
                    f"[STATUS] elapsed={now - started:.1f}s health={len(state['health'])} "
                    f"crc={state['crc_errors']} gaps={state['sequence_gap_events']}" + latest_text,
                    flush=True,
                )
                next_status = now + 30.0
    except KeyboardInterrupt:
        interrupted = True
        print("[STOP] interrupted", flush=True)
    finally:
        if port is not None:
            try:
                port.close()
            except (serial.SerialException, OSError):
                pass

    elapsed_s = time.monotonic() - started
    result = evaluate(args, expectation, state, elapsed_s, interrupted)
    try:
        write_result(args.output, result)
    except OSError as exc:
        print(f"[ERROR] could not write {args.output}: {exc}", file=sys.stderr, flush=True)
        return 2

    print(
        f"[{result['status']}] profile={expectation['name']} elapsed={elapsed_s:.1f}s "
        f"health={len(state['health'])} sessions={result['board_health']['boot_sessions']} "
        f"output={args.output}",
        flush=True,
    )
    for failure in result["failures"]:
        print(f"[FAILURE] {failure}", flush=True)
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
