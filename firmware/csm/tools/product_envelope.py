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
    usb_sink = ROOT / "include" / "board" / "uplink" / "UsbCdcSink.h"
    worker = ROOT / "include" / "board" / "uplink" / "WifiWorkerContract.h"
    product_profile = (
        ROOT / "include" / "board" / "uplink" / "ProductUplinkEnvelope.h"
    )
    typed_frame = ROOT / "include" / "protocol" / "TypedFrame.h"
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
    usb_queue_records = macro(usb_sink, "BOARD_USB_SINK_QUEUE_RECORDS")
    usb_queue_bytes = macro(usb_sink, "BOARD_USB_SINK_QUEUE_BYTES")
    usb_transient_coverage_ms = macro(
        usb_sink, "BOARD_USB_TRANSIENT_COVERAGE_MS"
    )
    stall_ms = macro(worker, "BOARD_WIFI_STALL_TIMEOUT_MS")
    transient_coverage_ms = macro(worker, "BOARD_WIFI_TRANSIENT_COVERAGE_MS")
    high_water_bytes = macro(worker, "BOARD_WIFI_PRESSURE_HIGH_WATER_BYTES")
    low_water_bytes = macro(worker, "BOARD_WIFI_PRESSURE_LOW_WATER_BYTES")
    high_water_records = macro(worker, "BOARD_WIFI_PRESSURE_HIGH_WATER_RECORDS")
    low_water_records = macro(worker, "BOARD_WIFI_PRESSURE_LOW_WATER_RECORDS")
    batch_bytes = macro(worker, "BOARD_WIFI_TX_BATCH_TARGET_BYTES")
    drain_budget_us = macro(worker, "BOARD_WIFI_TX_DRAIN_TIME_BUDGET_US")
    max_writes_per_pump = macro(worker, "BOARD_WIFI_TX_MAX_WRITES_PER_PUMP")
    max_bytes_per_pump = macro(worker, "BOARD_WIFI_TX_MAX_BYTES_PER_PUMP")
    fallback_ms = macro(worker, "BOARD_WIFI_CONNECTED_FALLBACK_MS")
    call_stall_ms = macro(worker, "BOARD_WIFI_CALL_STALL_TIMEOUT_MS")
    startup_attempt_limit = macro(worker, "BOARD_WIFI_STARTUP_ATTEMPT_LIMIT")
    startup_retry_ms = macro(worker, "BOARD_WIFI_STARTUP_RETRY_MS")
    tx_chunk_bytes = macro(worker, "BOARD_WIFI_TX_CHUNK_BYTES")
    ap_sta_concur = macro(worker, "BOARD_WIFI_AP_STA_CONCUR")
    can_queue = macro(main, "BOARD_CAN_QUEUE_SIZE")

    require(queue_records == 256, "product Wi-Fi descriptor envelope drift")
    require(queue_bytes == 49152, "product Wi-Fi byte envelope drift")
    require(reserve_records == 4, "critical descriptor reserve drift")
    require(reserve_bytes == 2112, "critical byte reserve drift")
    require(usb_queue_records == 208, "product USB descriptor envelope drift")
    require(usb_queue_bytes == 40960, "product USB byte envelope drift")
    require(usb_transient_coverage_ms == 0,
            "USB transient coverage must remain exploratory")
    require(stall_ms == 5000, "no-progress timeout drift")
    require(call_stall_ms == 5000, "socket call-stall boundary drift")
    require(transient_coverage_ms == 0,
            "Wi-Fi transient coverage must remain exploratory")
    require(high_water_bytes == 32768, "byte high-water drift")
    require(low_water_bytes == 8192, "byte low-water drift")
    require(high_water_records == 192, "record high-water drift")
    require(low_water_records == 64, "record low-water drift")
    require(batch_bytes == 1460, "TCP batch target drift")
    require(tx_chunk_bytes == 2920, "TCP write chunk drift")
    require(drain_budget_us == 2000, "nonblocking worker pump budget drift")
    require(max_writes_per_pump == 4, "worker write budget drift")
    require(max_bytes_per_pump == 11680, "worker byte budget drift")
    require(fallback_ms == 5, "worker fallback interval drift")
    require(startup_attempt_limit == 0, "product AP retry policy drift")
    require(startup_retry_ms == 2000, "product AP retry interval drift")
    require(ap_sta_concur == 1, "validated WHD compatibility mode drift")
    require(can_queue == 512, "per-bus CAN ingest envelope drift")
    for token in (
        "BOARD_WIFI_SINK_QUEUE_RECORDS=256",
        "BOARD_WIFI_SINK_QUEUE_BYTES=49152",
        "BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES=2112",
        "BOARD_WIFI_TRANSIENT_COVERAGE_MS=0",
        "BOARD_WIFI_PRESSURE_HIGH_WATER_BYTES=32768",
        "BOARD_WIFI_PRESSURE_LOW_WATER_BYTES=8192",
        "BOARD_WIFI_PRESSURE_HIGH_WATER_RECORDS=192",
        "BOARD_WIFI_PRESSURE_LOW_WATER_RECORDS=64",
        "BOARD_WIFI_STALL_TIMEOUT_MS=5000",
        "BOARD_WIFI_CALL_STALL_TIMEOUT_MS=5000",
        "BOARD_WIFI_AP_STA_CONCUR=1",
        "BOARD_USB_SINK_QUEUE_RECORDS=208",
        "BOARD_USB_SINK_QUEUE_BYTES=40960",
        "BOARD_USB_TRANSIENT_COVERAGE_MS=0",
    ):
        require(token in platformio, f"product environment missing {token}")
    for token in (
        ".wifi_tx_queue_dtcm (NOLOAD)",
        ".csm_dtcm_bss (NOLOAD)",
        "0xD000",
        "__csm_dtcm_bss_end__ <=",
        "LENGTH(DTCMRAM) - 0x10000",
    ):
        require(token in linker, f"linker ownership guard missing {token}")

    typed_overhead = constexpr(typed_frame, "kTypedFrameOverheadLen")
    compact_schema = constexpr(typed_records, "kCanRxSegmentSchema")
    compact_header = constexpr(typed_records, "kCanRxSegmentHeaderLen")
    compact_entry = constexpr(typed_records, "kCanRxSegmentEntryLen")
    compact_max = constexpr(typed_records, "kCanRxSegmentMaxFrames")
    transport_schema = constexpr(typed_records, "kTransportDiagnosticSchema")
    transport_payload = constexpr(
        typed_records, "kTransportDiagnosticPayloadLen"
    )
    require(compact_schema == 2, "compact CAN RX schema drift")
    require(transport_schema == 3, "transport diagnostic schema drift")
    require(transport_payload == 128, "transport diagnostic payload drift")
    rx_fps = (
        constexpr(product_profile, "kProductCanBusCount")
        * constexpr(product_profile, "kProductCanRxFramesPerSecondPerBus")
    )
    control_fps = constexpr(
        product_profile, "kProductControlCommandsPerSecond"
    )
    remote_state_fps = constexpr(
        product_profile, "kProductRemoteStateRecordsPerSecond"
    )
    board_health_fps = constexpr(
        product_profile, "kProductBoardHealthRecordsPerSecond"
    )
    transport_diagnostic_fps = constexpr(
        product_profile, "kProductTransportDiagnosticRecordsPerSecond"
    )
    def segmented_rate(
        fps: int, header: int, entry: int, frames_per_segment: int
    ) -> int:
        full, remainder = divmod(fps, frames_per_segment)
        total = full * (typed_overhead + header + entry * frames_per_segment)
        if remainder:
            total += typed_overhead + header + entry * remainder
        return total

    compact_rx = segmented_rate(
        rx_fps, compact_header, compact_entry, compact_max
    )
    can_tx = control_fps * (typed_overhead + constexpr(typed_records, "kCanRawPayloadLen"))
    control_ack = control_fps * (
        typed_overhead + constexpr(typed_records, "kControlAckPayloadLen")
    )
    control_tx_evidence = control_fps * (
        typed_overhead + constexpr(typed_records, "kControlTxEvidencePayloadLen")
    )
    remote_state = remote_state_fps * (
        typed_overhead + constexpr(typed_records, "kRemoteControlStatePayloadLen")
    )
    board_health = board_health_fps * (
        typed_overhead + constexpr(typed_records, "kBoardHealthV13PayloadLen")
    )
    transport_diagnostic = transport_diagnostic_fps * (
        typed_overhead + transport_payload
    )
    fixed_product = (
        can_tx + control_ack + control_tx_evidence + remote_state
        + board_health + transport_diagnostic
    )
    compact_total = compact_rx + fixed_product
    compact_records = math.ceil(rx_fps / compact_max)
    enabled_records = (
        compact_records + 3 * control_fps + remote_state_fps
        + board_health_fps + transport_diagnostic_fps
    )

    normal_bytes = queue_bytes - reserve_bytes
    normal_records = queue_records - reserve_records
    target_rate = compact_total
    live_fifo_min_batches = 4
    live_fifo_required_bytes = batch_bytes * live_fifo_min_batches
    max_encoded_frame_bytes = 523
    fallback_ingress_bytes = math.ceil(
        target_rate * fallback_ms / 1000
    )
    worker_service_bytes_per_second = max_bytes_per_pump * 1000 // fallback_ms
    pressure_guard_bytes = max_encoded_frame_bytes + fallback_ingress_bytes
    pressure_headroom_bytes = normal_bytes - high_water_bytes
    transient_ingress_bytes = math.ceil(
        target_rate * transient_coverage_ms / 1000
    )
    fallback_ingress_records = math.ceil(
        enabled_records * fallback_ms / 1000
    )
    transient_ingress_records = math.ceil(
        enabled_records * transient_coverage_ms / 1000
    )
    usb_transient_ingress_bytes = math.ceil(
        target_rate * usb_transient_coverage_ms / 1000
    )
    usb_transient_ingress_records = math.ceil(
        enabled_records * usb_transient_coverage_ms / 1000
    )
    usb_descriptor_bytes = usb_queue_records * 16
    usb_storage = usb_descriptor_bytes + usb_queue_bytes
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

    require(compact_rx == 88874, "aggregate CAN wire calculation regression")
    require(compact_total == 131222, "enabled product wire calculation regression")
    require(enabled_records == 1086, "enabled product record calculation regression")
    require(dtcm_storage == 53248, "DTCM live FIFO calculation regression")
    require(
        normal_bytes >= live_fifo_required_bytes,
        "live FIFO no longer holds four complete batch targets",
    )
    require(high_water_bytes < normal_bytes,
            "worker pressure threshold no longer precedes producer reserve")
    require(
        pressure_headroom_bytes >= pressure_guard_bytes,
        "worker pressure threshold cannot absorb one max frame plus fallback ingress",
    )
    require(
        normal_bytes >=
        transient_ingress_bytes + max_encoded_frame_bytes + fallback_ingress_bytes,
        "byte queue does not cover the declared transient envelope",
    )
    require(
        normal_records >=
        transient_ingress_records + 1 + fallback_ingress_records,
        "descriptor queue does not cover the declared transient envelope",
    )
    require(
        usb_queue_bytes >= usb_transient_ingress_bytes + max_encoded_frame_bytes,
        "USB byte queue does not cover the declared transient envelope",
    )
    require(
        usb_queue_records >= usb_transient_ingress_records + 1,
        "USB descriptor queue does not cover the declared transient envelope",
    )
    require(low_water_bytes < high_water_bytes < normal_bytes,
            "byte pressure hysteresis is invalid")
    require(low_water_records < high_water_records < normal_records,
            "record pressure hysteresis is invalid")
    require(dtcm_max_remaining >= 65536, "DTCM safety reserve violated")
    require(
        max_bytes_per_pump >= fallback_ingress_bytes,
        "worker pump byte budget cannot service design-rate fallback ingress",
    )
    require(
        max_writes_per_pump * tx_chunk_bytes >= max_bytes_per_pump,
        "worker write count cannot cover its byte budget",
    )

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
            "usb_queue_records": usb_queue_records,
            "usb_queue_bytes": usb_queue_bytes,
            "usb_transient_coverage_ms": usb_transient_coverage_ms,
            "critical_reserve_records": reserve_records,
            "critical_reserve_bytes": reserve_bytes,
            "stall_timeout_ms": stall_ms,
            "call_stall_timeout_ms": call_stall_ms,
            "transient_coverage_ms": transient_coverage_ms,
            "pressure_high_water_bytes": high_water_bytes,
            "pressure_low_water_bytes": low_water_bytes,
            "pressure_high_water_records": high_water_records,
            "pressure_low_water_records": low_water_records,
            "batch_target_bytes": batch_bytes,
            "tx_chunk_bytes": tx_chunk_bytes,
            "drain_time_budget_us": drain_budget_us,
            "max_writes_per_pump": max_writes_per_pump,
            "max_bytes_per_pump": max_bytes_per_pump,
            "connected_fallback_ms": fallback_ms,
            "call_stall_timeout_ms": call_stall_ms,
            "startup_attempt_limit": startup_attempt_limit,
            "startup_retry_ms": startup_retry_ms,
            "whd_ap_sta_concur": ap_sta_concur,
            "can_queue_per_bus": can_queue,
        },
        "throughput_bytes_per_second": {
            "aggregate_can_rx_4000fps": compact_rx,
            "enabled_profile_exact": compact_total,
            "enabled_records_per_second": enabled_records,
        },
        "live_fifo_envelope": {
            "target_rate_bytes_per_second": target_rate,
            "minimum_batch_count": live_fifo_min_batches,
            "required_bytes": live_fifo_required_bytes,
            "normal_admission_bytes": normal_bytes,
            "byte_margin": normal_bytes - live_fifo_required_bytes,
            "byte_coverage_seconds": round(normal_bytes / target_rate, 5),
            "normal_admission_records": normal_records,
            "high_water_bytes": high_water_bytes,
            "high_water_records": high_water_records,
            "low_water_bytes": low_water_bytes,
            "low_water_records": low_water_records,
            "max_encoded_frame_bytes": max_encoded_frame_bytes,
            "fallback_ingress_bytes": fallback_ingress_bytes,
            "fallback_ingress_records": fallback_ingress_records,
            "transient_ingress_bytes": transient_ingress_bytes,
            "transient_ingress_records": transient_ingress_records,
            "pressure_guard_bytes": pressure_guard_bytes,
            "pressure_headroom_bytes": pressure_headroom_bytes,
            "high_water_seconds_at_target": round(high_water_bytes / target_rate, 5),
            "worker_service_bytes_per_second": worker_service_bytes_per_second,
        },
        "usb_fifo_envelope": {
            "queue_bytes": usb_queue_bytes,
            "queue_records": usb_queue_records,
            "transient_ingress_bytes": usb_transient_ingress_bytes,
            "transient_ingress_records": usb_transient_ingress_records,
            "max_encoded_frame_bytes": max_encoded_frame_bytes,
            "storage_bytes": usb_storage,
        },
        "memory_bytes": {
            "wifi_descriptors": descriptor_bytes,
            "wifi_byte_arena": queue_bytes,
            "wifi_dtcm_storage": dtcm_storage,
            "usb_descriptors": usb_descriptor_bytes,
            "usb_byte_arena": usb_queue_bytes,
            "usb_d1_storage": usb_storage,
            "dtcm_usable": dtcm_usable,
            "dtcm_alignment_pad_before_queue": dtcm_alignment_pad,
            "dtcm_max_remaining_after_queue": dtcm_max_remaining,
            "dtcm_linker_reserved_minimum": 65536,
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
            "threshold_qualification_complete": False,
            "live_fifo_holds_four_batches": normal_bytes
            >= live_fifo_required_bytes,
            "worker_pressure_precedes_reserve": high_water_bytes < normal_bytes,
            "worker_pressure_protects_reserve": pressure_headroom_bytes
            >= pressure_guard_bytes,
            "worker_pump_services_enabled_ingress": max_bytes_per_pump
            >= fallback_ingress_bytes,
            "transient_byte_coverage": normal_bytes
            >= transient_ingress_bytes + max_encoded_frame_bytes
            + fallback_ingress_bytes,
            "transient_record_coverage": normal_records
            >= transient_ingress_records + 1 + fallback_ingress_records,
            "usb_transient_byte_coverage": usb_queue_bytes
            >= usb_transient_ingress_bytes + max_encoded_frame_bytes,
            "usb_transient_record_coverage": usb_queue_records
            >= usb_transient_ingress_records + 1,
            "pressure_hysteresis_valid": (
                low_water_bytes < high_water_bytes < normal_bytes
                and low_water_records < high_water_records < normal_records
            ),
            "dtcm_safety_reserve_64k": dtcm_max_remaining >= 65536,
        },
    }


def markdown(report: dict) -> str:
    throughput = report["throughput_bytes_per_second"]
    live_fifo = report["live_fifo_envelope"]
    usb_fifo = report["usb_fifo_envelope"]
    memory = report["memory_bytes"]
    gates = report["gates"]
    rows = [
        (
            "Aggregate CAN RX 4,000fps",
            f"{throughput['aggregate_can_rx_4000fps']:,} B/s",
            "PASS",
        ),
        (
            "Enabled profile exact",
            f"{throughput['enabled_profile_exact']:,} B/s, "
            f"{throughput['enabled_records_per_second']:,} records/s",
            "PASS",
        ),
        (
            "Four TCP batches / normal live FIFO",
            f"{live_fifo['required_bytes']:,} / "
            f"{live_fifo['normal_admission_bytes']:,} B",
            "PASS" if gates["live_fifo_holds_four_batches"] else "FAIL",
        ),
        (
            "Exploratory byte coverage / normal live FIFO",
            f"{live_fifo['transient_ingress_bytes'] + live_fifo['max_encoded_frame_bytes'] + live_fifo['fallback_ingress_bytes']:,} / "
            f"{live_fifo['normal_admission_bytes']:,} B",
            "OPEN",
        ),
        (
            "Exploratory record coverage / normal descriptors",
            f"{live_fifo['transient_ingress_records'] + 1 + live_fifo['fallback_ingress_records']:,} / "
            f"{live_fifo['normal_admission_records']:,}",
            "OPEN",
        ),
        (
            "USB exploratory byte coverage / byte FIFO",
            f"{usb_fifo['transient_ingress_bytes'] + usb_fifo['max_encoded_frame_bytes']:,} / "
            f"{usb_fifo['queue_bytes']:,} B",
            "OPEN",
        ),
        (
            "USB exploratory record coverage / descriptors",
            f"{usb_fifo['transient_ingress_records'] + 1:,} / "
            f"{usb_fifo['queue_records']:,}",
            "OPEN",
        ),
        (
            "Pressure headroom / max-frame + fallback ingress",
            f"{live_fifo['pressure_headroom_bytes']:,} / "
            f"{live_fifo['pressure_guard_bytes']:,} B",
            "PASS"
            if gates["worker_pressure_protects_reserve"]
            else "FAIL",
        ),
        (
            "Pressure byte/record hysteresis",
            f"{live_fifo['low_water_bytes']:,}/{live_fifo['high_water_bytes']:,} B, "
            f"{live_fifo['low_water_records']}/{live_fifo['high_water_records']} records",
            "PASS" if gates["pressure_hysteresis_valid"] else "FAIL",
        ),
        (
            "Worker service capacity / enabled exact rate",
            f"{live_fifo['worker_service_bytes_per_second']:,} / "
            f"{throughput['enabled_profile_exact']:,} B/s",
            "PASS"
            if gates["worker_pump_services_enabled_ingress"]
            else "FAIL",
        ),
        (
            "DTCM remaining / enforced reserve",
            f"{memory['dtcm_max_remaining_after_queue']:,} / "
            f"{memory['dtcm_linker_reserved_minimum']:,} B",
            "PASS" if gates["dtcm_safety_reserve_64k"] else "FAIL",
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
