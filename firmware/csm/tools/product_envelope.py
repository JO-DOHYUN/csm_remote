#!/usr/bin/env python3
"""Calculate and guard the CSM product data/memory envelope.

This is deliberately source-backed: a build-time constant drift changes this
table or fails the guard instead of leaving a stale spreadsheet as authority.
"""

from __future__ import annotations

import argparse
import json
import math
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def macro(path: Path, name: str) -> int:
    text = path.read_text(encoding="utf-8")
    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+([0-9]+)(?:[uUlL]*)\s*$",
        text,
        re.MULTILINE,
    )
    if match is None:
        raise RuntimeError(f"{name} not found in {path}")
    return int(match.group(1))


def constexpr(path: Path, name: str) -> int:
    text = path.read_text(encoding="utf-8")
    match = re.search(
        rf"^\s*static\s+constexpr\s+\w+\s+{re.escape(name)}\s*=\s*"
        rf"([0-9]+)(?:[uUlL]*)\s*;\s*$",
        text,
        re.MULTILINE,
    )
    if match is None:
        raise RuntimeError(f"{name} not found in {path}")
    return int(match.group(1))


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def calculate() -> dict:
    mailbox = ROOT / "include" / "board" / "uplink" / "WifiWorkerMailbox.h"
    worker = ROOT / "include" / "board" / "uplink" / "WifiWorkerContract.h"
    socket_worker = ROOT / "include" / "board" / "uplink" / "WifiSocketWorker.h"
    typed_records = ROOT / "include" / "protocol" / "TypedRecords.h"
    main = ROOT / "src" / "main.cpp"
    platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    linker = (ROOT / "linker" / "portenta_h7_m7_product.ld").read_text(
        encoding="utf-8"
    )

    queue_records = macro(mailbox, "BOARD_WIFI_SINK_QUEUE_RECORDS")
    queue_bytes = macro(mailbox, "BOARD_WIFI_SINK_QUEUE_BYTES")
    reserve_records = macro(mailbox, "BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS")
    reserve_bytes = macro(mailbox, "BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES")
    stall_ms = macro(worker, "BOARD_WIFI_STALL_TIMEOUT_MS")
    high_water_percent = macro(worker, "BOARD_WIFI_ISOLATE_HIGH_WATER_PERCENT")
    batch_bytes = macro(worker, "BOARD_WIFI_TX_BATCH_TARGET_BYTES")
    drain_budget_us = macro(worker, "BOARD_WIFI_TX_DRAIN_TIME_BUDGET_US")
    call_stall_ms = macro(worker, "BOARD_WIFI_CALL_STALL_TIMEOUT_MS")
    startup_attempt_limit = macro(worker, "BOARD_WIFI_STARTUP_ATTEMPT_LIMIT")
    startup_retry_ms = macro(worker, "BOARD_WIFI_STARTUP_RETRY_MS")
    tx_chunk_bytes = macro(socket_worker, "BOARD_WIFI_TX_CHUNK_BYTES")
    ap_sta_concur = macro(worker, "BOARD_WIFI_AP_STA_CONCUR")
    can_queue = macro(main, "BOARD_CAN_QUEUE_SIZE")

    require(queue_records == 1280, "product Wi-Fi descriptor envelope drift")
    require(queue_bytes == 65520, "product Wi-Fi byte envelope drift")
    require(reserve_records == 4, "critical descriptor reserve drift")
    require(reserve_bytes == 2112, "critical byte reserve drift")
    require(stall_ms == 5000, "no-progress timeout drift")
    require(high_water_percent == 96, "pre-full isolation threshold drift")
    require(batch_bytes == 1024, "TCP batch target drift")
    require(tx_chunk_bytes == 1024, "TCP write chunk drift")
    require(drain_budget_us == 1000, "nonblocking worker pump budget drift")
    require(call_stall_ms == 250, "socket call-stall boundary drift")
    require(startup_attempt_limit == 0, "product AP retry policy drift")
    require(startup_retry_ms == 2000, "product AP retry interval drift")
    require(ap_sta_concur == 1, "validated WHD compatibility mode drift")
    require(can_queue == 512, "per-bus CAN ingest envelope drift")
    for token in (
        "BOARD_WIFI_SINK_QUEUE_RECORDS=1280",
        "BOARD_WIFI_SINK_QUEUE_BYTES=65520",
        "BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES=2112",
        "BOARD_WIFI_STALL_TIMEOUT_MS=5000",
        "BOARD_WIFI_AP_STA_CONCUR=1",
    ):
        require(token in platformio, f"product environment missing {token}")
    for token in (
        ".wifi_tx_queue_dtcm (NOLOAD)",
        ".csm_dtcm_bss (NOLOAD)",
        "0x14FF0",
        "LENGTH(DTCMRAM) - 0x8000",
    ):
        require(token in linker, f"linker ownership guard missing {token}")

    typed_overhead = 11
    compact_schema = constexpr(typed_records, "kCanRxSegmentSchema")
    compact_header = constexpr(typed_records, "kCanRxSegmentHeaderLen")
    compact_entry = constexpr(typed_records, "kCanRxSegmentEntryLen")
    compact_max = constexpr(typed_records, "kCanRxSegmentMaxFrames")
    legacy_header = constexpr(typed_records, "kCanRxSegmentLegacyHeaderLen")
    legacy_entry = constexpr(typed_records, "kCanRxSegmentLegacyEntryLen")
    legacy_max = constexpr(typed_records, "kCanRxSegmentLegacyMaxFrames")
    transport_schema = constexpr(typed_records, "kTransportDiagnosticSchema")
    transport_payload = constexpr(
        typed_records, "kTransportDiagnosticPayloadLen"
    )
    require(compact_schema == 2, "compact CAN RX schema drift")
    require(transport_schema == 2, "transport diagnostic schema drift")
    require(transport_payload == 128, "transport diagnostic payload drift")
    rx_fps = 2000

    def segmented_rate(
        fps: int, header: int, entry: int, frames_per_segment: int
    ) -> int:
        full, remainder = divmod(fps, frames_per_segment)
        total = full * (typed_overhead + header + entry * frames_per_segment)
        if remainder:
            total += typed_overhead + header + entry * remainder
        return total

    legacy_rx = segmented_rate(
        rx_fps, legacy_header, legacy_entry, legacy_max
    )
    compact_rx = segmented_rate(
        rx_fps, compact_header, compact_entry, compact_max
    )
    compact_rx_4000 = segmented_rate(
        4000, compact_header, compact_entry, compact_max
    )

    can_tx = 250 * 41
    remote_state = 10 * 239
    board_health = 519
    transport_diagnostic = 139
    fixed_product = can_tx + remote_state + board_health + transport_diagnostic
    legacy_total = legacy_rx + fixed_product
    compact_total = compact_rx + fixed_product
    compact_total_4000 = compact_rx_4000 + fixed_product

    measured_raw_low = 65083
    measured_raw_high = 69413
    normal_bytes = queue_bytes - reserve_bytes
    normal_records = queue_records - reserve_records
    target_rate = 100_000 / 8
    target_stall_seconds = 5
    target_stall_bytes = int(target_rate * target_stall_seconds)
    high_water_bytes = math.ceil(queue_bytes * high_water_percent / 100)
    descriptor_bytes = queue_records * 16
    dtcm_storage = descriptor_bytes + queue_bytes
    dtcm_usable = 130408
    dtcm_alignment = 32
    dtcm_origin_offset = 664
    dtcm_alignment_pad = (-dtcm_origin_offset) % dtcm_alignment
    dtcm_max_remaining = dtcm_usable - dtcm_alignment_pad - dtcm_storage
    can_item_bytes = 32
    can_queue_total = 2 * can_queue * can_item_bytes
    legacy_can_queue_total = 2 * 4096 * can_item_bytes

    require(legacy_rx == 65762, "legacy wire calculation regression")
    require(compact_rx == 44437, "compact wire calculation regression")
    require(compact_total == 57735, "product wire calculation regression")
    require(dtcm_storage == 86000, "DTCM queue calculation regression")
    require(normal_bytes >= target_stall_bytes, "5-second byte envelope fails")
    require(high_water_bytes < normal_bytes, "high-water no longer precedes reserve")
    require(dtcm_max_remaining >= 32768, "DTCM safety reserve violated")

    return {
        "schema": 1,
        "source_contract": {
            "can_rx_segment_schema": compact_schema,
            "can_rx_segment_header_bytes": compact_header,
            "can_rx_segment_entry_bytes": compact_entry,
            "can_rx_segment_max_frames": compact_max,
            "transport_diagnostic_schema": transport_schema,
            "transport_diagnostic_payload_bytes": transport_payload,
            "wifi_queue_records": queue_records,
            "wifi_queue_bytes": queue_bytes,
            "critical_reserve_records": reserve_records,
            "critical_reserve_bytes": reserve_bytes,
            "stall_timeout_ms": stall_ms,
            "isolate_high_water_percent": high_water_percent,
            "batch_target_bytes": batch_bytes,
            "tx_chunk_bytes": tx_chunk_bytes,
            "drain_time_budget_us": drain_budget_us,
            "call_stall_timeout_ms": call_stall_ms,
            "startup_attempt_limit": startup_attempt_limit,
            "startup_retry_ms": startup_retry_ms,
            "whd_ap_sta_concur": ap_sta_concur,
            "can_queue_per_bus": can_queue,
        },
        "throughput_bytes_per_second": {
            "legacy_can_rx_2000fps": legacy_rx,
            "compact_can_rx_2000fps": compact_rx,
            "fixed_product_records": fixed_product,
            "legacy_product_total_2000fps": legacy_total,
            "compact_product_total_2000fps": compact_total,
            "compact_product_total_4000fps": compact_total_4000,
            "measured_raw_low": measured_raw_low,
            "measured_raw_high": measured_raw_high,
            "compact_margin_low_percent": round(
                (measured_raw_low - compact_total) * 100 / measured_raw_low, 2
            ),
            "compact_margin_high_percent": round(
                (measured_raw_high - compact_total) * 100 / measured_raw_high, 2
            ),
        },
        "stall_envelope": {
            "target_rate_bytes_per_second": target_rate,
            "target_seconds": target_stall_seconds,
            "required_bytes": target_stall_bytes,
            "normal_admission_bytes": normal_bytes,
            "byte_margin": normal_bytes - target_stall_bytes,
            "byte_coverage_seconds": round(normal_bytes / target_rate, 5),
            "normal_admission_records": normal_records,
            "high_water_bytes": high_water_bytes,
            "high_water_seconds_at_target": round(high_water_bytes / target_rate, 5),
        },
        "memory_bytes": {
            "wifi_descriptors": descriptor_bytes,
            "wifi_byte_arena": queue_bytes,
            "wifi_dtcm_storage": dtcm_storage,
            "dtcm_usable": dtcm_usable,
            "dtcm_alignment_pad_before_queue": dtcm_alignment_pad,
            "dtcm_max_remaining_after_queue": dtcm_max_remaining,
            "dtcm_linker_reserved_minimum": 32768,
            "can_ingest_queues": can_queue_total,
            "can_ingest_saved_vs_4096": legacy_can_queue_total - can_queue_total,
        },
        "timing": {
            "can_queue_coverage_ms_at_2000fps_per_bus": round(
                can_queue * 1000 / 2000, 2
            ),
            "can_queue_coverage_ms_at_3400fps_per_bus": round(
                can_queue * 1000 / 3400, 2
            ),
        },
        "gates": {
            "compact_2000fps_fits_measured_raw_low": compact_total
            < measured_raw_low,
            "compact_4000fps_fits_measured_raw_high": compact_total_4000
            < measured_raw_high,
            "five_second_100kbps_queue_fits": normal_bytes
            >= target_stall_bytes,
            "pre_full_isolation_precedes_reserve": high_water_bytes
            < normal_bytes,
            "dtcm_safety_reserve_32k": dtcm_max_remaining >= 32768,
        },
    }


def markdown(report: dict) -> str:
    throughput = report["throughput_bytes_per_second"]
    stall = report["stall_envelope"]
    memory = report["memory_bytes"]
    gates = report["gates"]
    rows = [
        (
            "CAN RX 2,000fps legacy -> compact",
            f"{throughput['legacy_can_rx_2000fps']:,} -> "
            f"{throughput['compact_can_rx_2000fps']:,} B/s",
            "PASS",
        ),
        (
            "2,000fps product total / raw low",
            f"{throughput['compact_product_total_2000fps']:,} / "
            f"{throughput['measured_raw_low']:,} B/s",
            "PASS" if gates["compact_2000fps_fits_measured_raw_low"] else "FAIL",
        ),
        (
            "4,000fps product total / raw high",
            f"{throughput['compact_product_total_4000fps']:,} / "
            f"{throughput['measured_raw_high']:,} B/s",
            "PASS" if gates["compact_4000fps_fits_measured_raw_high"] else "GATE",
        ),
        (
            "100kbps x 5s / normal queue",
            f"{stall['required_bytes']:,} / "
            f"{stall['normal_admission_bytes']:,} B",
            "PASS" if gates["five_second_100kbps_queue_fits"] else "FAIL",
        ),
        (
            "Wi-Fi DTCM / usable",
            f"{memory['wifi_dtcm_storage']:,} / "
            f"{memory['dtcm_usable']:,} B",
            "PASS" if gates["dtcm_safety_reserve_32k"] else "FAIL",
        ),
    ]
    lines = [
        "| Gate | Calculated value | Result |",
        "|---|---:|:---:|",
    ]
    lines.extend(f"| {name} | {value} | {result} |" for name, value, result in rows)
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--format", choices=("json", "markdown"), default="markdown")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = calculate()
    rendered = (
        json.dumps(report, indent=2, ensure_ascii=False) + "\n"
        if args.format == "json"
        else markdown(report)
    )
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
