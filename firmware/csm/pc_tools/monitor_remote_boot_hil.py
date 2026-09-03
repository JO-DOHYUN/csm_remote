"""Correlate CSM USB diagnostics and physical Kvaser CAN evidence.

The tool deliberately opens and places the physical CAN channel on-bus before
it observes or opens USB CDC.  Every source is timestamped with the host's
``monotonic_ns`` clock and raw evidence is kept even when a strict fault ends
the requested run early.

This is a debug/HIL tool.  It does not send USB or CAN traffic and it never
flushes the USB input buffer.
"""

import argparse
import bisect
import ctypes
import datetime as dt
import hashlib
import json
import os
import pathlib
import platform
import queue
import statistics
import sys
import threading
import time
import traceback
from ctypes import byref, c_int, c_long, c_size_t, c_uint, c_ubyte

import serial
from serial.tools import list_ports

from verify_typed_stream import i16, i32, parse_frame, u16, u32, u64, zstr


CAN_OK = 0
CAN_ERR_NOMSG = -2
CAN_BITRATE_500K = -2
CAN_DRIVER_NORMAL = 4
CANMSG_ERROR_FRAME = 0x20
CAN_CHANNELDATA_CHANNEL_NAME = 13

DEFAULT_VID = 0x2341
DEFAULT_PID = 0x025B
CONTROL_IDS = (0x503, 0x510, 0x512, 0x511, 0x513)
FAULT_TAIL_SECONDS = 5.0
CORRELATION_FUTURE_SLACK_MS = 20.0
RAW_TO_OUTCOME_MAX_US = 15000
TYPE_NAMES = {
    2: "CAN_TX_RAW",
    7: "BOARD_EVENT",
    8: "BOARD_HEALTH",
    9: "CAPABILITY",
    17: "STREAM_SESSION",
    18: "REMOTE_CONTROL_STATE",
    19: "RUNTIME_DIAGNOSTIC",
}
DIAGNOSTIC_PHASES = {
    1: "boot",
    2: "periodic",
    3: "tx_before_retained",
    4: "tx_return_retained",
    5: "tx_outcome",
    6: "recovered",
    7: "recovery_event",
    8: "recovered_wifi_call",
}
WIFI_CALL_PHASES = {
    0: "idle",
    1: "configure_ip",
    2: "begin_access_point",
    3: "begin_server",
    4: "accept_client",
    5: "configure_client",
    6: "send",
    7: "receive",
    8: "close_client",
    9: "delete_client",
    10: "accept_extra_client",
    11: "close_extra_client",
    12: "delete_extra_client",
    13: "stop_server",
    14: "stop_access_point",
    15: "begin_control_server",
    16: "accept_control_client",
    17: "configure_control_client",
    18: "send_control",
    19: "receive_control",
    20: "close_control_client",
    21: "stop_control_server",
    22: "open_telemetry_server",
    23: "configure_telemetry_server",
    24: "bind_telemetry_server",
    25: "listen_telemetry_server",
    26: "open_control_server",
    27: "configure_control_server",
    28: "bind_control_server",
    29: "listen_control_server",
}
FDCAN_REGISTER_NAMES = (
    "CCCR",
    "PSR",
    "ECR",
    "TXFQS",
    "TXBRP",
    "TXBTO",
    "TXBCF",
    "IR",
)
LEC_NAMES = {
    0: "no_error",
    1: "stuff_error",
    2: "form_error",
    3: "ack_error",
    4: "bit1_error",
    5: "bit0_error",
    6: "crc_error",
    7: "no_change",
}
BOARD_EVENT_NAMES = {
    1: "BOOT",
    2: "CAN_BEGIN_FAILED",
    9: "MCP2515_ERROR",
    10: "MCP2515_SPI_SNAPSHOT",
    11: "BUILTIN_CAN_BEGIN_FAILED",
    12: "BUILTIN_CAN_TX_FAILED",
    17: "MCP2515_TX_FAILED",
    32: "MCP_PASSIVE_MODE_VIOLATION",
    33: "MCP_TXREQ_VIOLATION",
    35: "USB_POWER_OR_RESET_SUSPECTED",
    38: "CAN_FRONTEND_SESSION_INIT_FAILED",
    39: "CAN_FRONTEND_FAULT_HOLD",
    41: "RUNTIME_BREADCRUMB_RECOVERED",
    42: "REMOTE_CONTROL_INIT_FAILED",
    43: "REMOTE_CONTROL_STATE_CHANGED",
}
STRICT_BOARD_EVENTS = {2, 9, 11, 12, 17, 32, 33, 35, 38, 39, 42}


def parse_int(value):
    return int(value, 0)


def utc_now():
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="milliseconds")


def json_line(handle, value):
    handle.write(json.dumps(value, ensure_ascii=False, sort_keys=True) + "\n")


def safe_name(value):
    return "".join(ch if ch.isalnum() or ch in "-_." else "_" for ch in value)


def percentile(values, ratio):
    if not values:
        return None
    ordered = sorted(values)
    index = min(len(ordered) - 1, int((len(ordered) - 1) * ratio))
    return ordered[index]


def counter_delta(first, last):
    return (last - first) & 0xFFFFFFFF


def extract_control_bursts(frames):
    candidates = 0
    complete = []
    incomplete = []
    for index, frame in enumerate(frames):
        if frame["can_id"] != CONTROL_IDS[0]:
            continue
        candidates += 1
        burst_frames = frames[index : index + len(CONTROL_IDS)]
        observed_ids = tuple(item["can_id"] for item in burst_frames)
        if len(burst_frames) == len(CONTROL_IDS) and observed_ids == CONTROL_IDS:
            complete.append({
                "candidate_index": index,
                "frames": burst_frames,
                "signature": tuple(
                    (item["can_id"], item["dlc"], item["data"])
                    for item in burst_frames
                ),
            })
        else:
            incomplete.append({
                "candidate_index": index,
                "observed_ids": [f"0x{value:X}" for value in observed_ids],
                "trailing_capture_boundary": index + len(CONTROL_IDS) > len(frames),
            })
    return {
        "candidates": candidates,
        "complete_count": len(complete),
        "incomplete_count": len(incomplete),
        "non_trailing_incomplete_count": sum(
            1 for item in incomplete if not item["trailing_capture_boundary"]
        ),
        "incomplete_examples": incomplete[:20],
        "complete": complete,
    }


def analyze_rolling_nibble(frames):
    manual = [item for item in frames if item["can_id"] == 0x503]
    result = {
        "frames": len(manual),
        "applicable": False,
        "reason": None,
        "checksum_failures": 0,
        "continuity_failures": 0,
        "failure_examples": [],
    }
    if len(manual) < 2:
        result["reason"] = "fewer_than_two_0x503_frames"
        return result
    decoded_data = []
    for index, frame in enumerate(manual):
        data = bytes.fromhex(frame["data"])
        if frame["dlc"] < 8 or len(data) < 8:
            result["reason"] = "0x503_dlc_below_8"
            return result
        if data[0] >> 6 != 0:
            result["reason"] = "0x503_source_is_not_remote"
            return result
        decoded_data.append((index, data))
    result["applicable"] = True
    result["reason"] = "remote_mapper_byte7_alive_and_checksum_encoding"
    nibbles = []
    for index, data in decoded_data:
        alive = data[7] >> 4
        checksum = data[7] & 0x0F
        expected_checksum = sum(data[:7]) & 0x0F
        nibbles.append(alive)
        if checksum != expected_checksum:
            result["checksum_failures"] += 1
            if len(result["failure_examples"]) < 20:
                result["failure_examples"].append({
                    "kind": "checksum",
                    "frame_index": index,
                    "actual": checksum,
                    "expected": expected_checksum,
                })
    for index in range(1, len(nibbles)):
        expected = (nibbles[index - 1] + 1) & 0x0F
        if nibbles[index] != expected:
            result["continuity_failures"] += 1
            if len(result["failure_examples"]) < 20:
                result["failure_examples"].append({
                    "kind": "rolling_nibble",
                    "frame_index": index,
                    "previous": nibbles[index - 1],
                    "actual": nibbles[index],
                    "expected": expected,
                })
    result["first"] = nibbles[0]
    result["last"] = nibbles[-1]
    return result


def port_description(port):
    return {
        "device": port.device,
        "description": port.description,
        "hwid": port.hwid,
        "vid": port.vid,
        "pid": port.pid,
        "serial_number": port.serial_number,
        "location": port.location,
        "manufacturer": port.manufacturer,
        "product": port.product,
        "interface": port.interface,
    }


def matching_ports(vid, pid, serial_number):
    expected_serial = serial_number.casefold() if serial_number else None
    matches = []
    for port in list_ports.comports():
        if port.vid != vid or port.pid != pid:
            continue
        actual_serial = (port.serial_number or "").casefold()
        if expected_serial is not None and actual_serial != expected_serial:
            continue
        matches.append(port)
    return sorted(matches, key=lambda item: item.device.casefold())


def decode_fdcan_registers(payload, offset):
    raw = {
        name: u32(payload, offset + index * 4)
        for index, name in enumerate(FDCAN_REGISTER_NAMES)
    }
    psr = raw["PSR"]
    ecr = raw["ECR"]
    txfqs = raw["TXFQS"]
    lec = psr & 0x7
    dlec = (psr >> 8) & 0x7
    decoded = {
        "raw": {name: f"0x{value:08X}" for name, value in raw.items()},
        "lec": lec,
        "lec_name": LEC_NAMES[lec],
        "activity": (psr >> 3) & 0x3,
        "error_passive": bool(psr & (1 << 5)),
        "warning": bool(psr & (1 << 6)),
        "bus_off": bool(psr & (1 << 7)),
        "dlec": dlec,
        "dlec_name": LEC_NAMES[dlec],
        "protocol_exception": bool(psr & (1 << 14)),
        "tec": ecr & 0xFF,
        "rec": (ecr >> 8) & 0x7F,
        "receive_error_passive": bool(ecr & (1 << 15)),
        "error_logging_counter": (ecr >> 16) & 0xFF,
        "tx_fifo_free_level": txfqs & 0x3F,
        "tx_get_index": (txfqs >> 8) & 0x1F,
        "tx_put_index": (txfqs >> 16) & 0x1F,
        "tx_fifo_full": bool(txfqs & (1 << 21)),
    }
    faults = []
    if decoded["error_passive"]:
        faults.append("EP")
    if decoded["warning"]:
        faults.append("EW")
    if decoded["bus_off"]:
        faults.append("BO")
    if 1 <= lec <= 6:
        faults.append(f"LEC={LEC_NAMES[lec]}")
    if 1 <= dlec <= 6:
        faults.append(f"DLEC={LEC_NAMES[dlec]}")
    if decoded["tec"] != 0 or decoded["rec"] != 0:
        faults.append(f"ECR(TEC={decoded['tec']},REC={decoded['rec']})")
    if decoded["receive_error_passive"]:
        faults.append("ECR.RP")
    decoded["faults"] = faults
    return raw, decoded


def decode_stream_session(payload):
    if len(payload) < 32:
        return None
    return {
        "schema": payload[0],
        "reason": payload[1],
        "flags": u16(payload, 2),
        "transport_version": payload[4],
        "boot_session_id": u64(payload, 8),
        "publish_seq64": u64(payload, 16),
        "mono_us": u64(payload, 24),
    }


def decode_can_tx_raw(payload):
    if len(payload) < 30:
        return None
    can_id_flags = u32(payload, 8)
    extended = bool(can_id_flags & (1 << 29))
    dlc = payload[12] & 0x0F
    data_length = min(dlc, 8)
    return {
        "mono_us": u64(payload, 0),
        "can_id_flags": can_id_flags,
        "can_id": can_id_flags & (0x1FFFFFFF if extended else 0x7FF),
        "extended": extended,
        "rtr": bool(can_id_flags & (1 << 30)),
        "dlc": dlc,
        "bus": payload[13],
        "data": payload[14 : 14 + data_length].hex(),
        "data_padded": payload[14:22].hex(),
        "tx_total": u32(payload, 22),
        "tx_failed_total": u32(payload, 26),
    }


def decode_board_event(payload):
    if len(payload) < 16:
        return None
    code = u16(payload, 8)
    return {
        "mono_us": u64(payload, 0),
        "code": code,
        "name": BOARD_EVENT_NAMES.get(code, f"code_{code}"),
        "detail": u16(payload, 10),
        "counter": u32(payload, 12),
    }


def decode_board_health(payload):
    if len(payload) < 52:
        return None
    result = {
        "mono_us": u64(payload, 0),
        "can_rx_total": u32(payload, 8),
        "can_drop_total": u32(payload, 12),
        "fifo_overflow_total": u32(payload, 16),
        "tx_records_total": u32(payload, 20),
        "queue_depth": u32(payload, 24),
        "safety_state": payload[44],
        "flags": payload[47],
        "fault_bits": u32(payload, 48),
        "payload_length": len(payload),
    }
    if len(payload) >= 128:
        result.update({
            "health_version": payload[52],
            "health_length_low8": payload[53],
            "mcp_tx_total": u32(payload, 80),
            "mcp_tx_fail_total": u32(payload, 84),
            "builtin_tx_total": u32(payload, 88),
            "builtin_tx_fail_total": u32(payload, 92),
            "mcp_spi_error_total": u32(payload, 96),
            "mcp_error_flags_total": u32(payload, 100),
            "mcp_canintf": payload[104],
            "mcp_eflg": payload[105],
            "mcp_canctrl": payload[106],
            "mcp_int_low": payload[107],
        })
    if len(payload) >= 260:
        result.update({
            "firmware_profile": u32(payload, 224),
            "vehicle_impact_state": u32(payload, 228),
            "can_rx_task_max_us": u32(payload, 232),
            "usb_reconnect_count": u32(payload, 244),
            "usb_forced_reset_count": u32(payload, 248),
            "capture_invalid_reason": u32(payload, 256),
        })
    if len(payload) >= 360:
        result.update({
            "canonical_publish_seq_next": u64(payload, 296),
            "boot_session_id": u64(payload, 304),
            "usb_connection_epoch": u32(payload, 312),
            "usb_offer_overflow_total": u32(payload, 320),
            "wifi_connection_epoch": u32(payload, 328),
            "wifi_offer_overflow_total": u32(payload, 336),
            "wifi_connect_total": u32(payload, 344),
            "wifi_disconnect_total": u32(payload, 348),
            "publisher_no_sink_drop_total": u32(payload, 356),
        })
    if len(payload) >= 384:
        result.update({
            "wifi_socket_error_total": u32(payload, 360),
            "wifi_send_budget_overrun_total": u32(payload, 364),
            "wifi_send_call_max_us": u32(payload, 368),
            "wifi_recv_call_max_us": u32(payload, 372),
            "wifi_close_call_max_us": u32(payload, 376),
            "main_loop_max_gap_us": u32(payload, 380),
        })
    if len(payload) >= 392:
        result.update({
            "reset_cause_bits": u32(payload, 384),
            "reset_status_raw": u32(payload, 388),
        })
    if len(payload) >= 408:
        packed_stage = u32(payload, 396)
        result.update({
            "previous_runtime_breadcrumb_valid": u32(payload, 392),
            "previous_runtime_breadcrumb_stage": packed_stage & 0xFF,
            "previous_runtime_breadcrumb_detail": (packed_stage >> 8) & 0xFF,
            "previous_runtime_breadcrumb_uptime_ms": u32(payload, 400),
            "previous_runtime_breadcrumb_write_sequence": u32(payload, 404),
        })
    if len(payload) >= 472 and payload[52] >= 12:
        recovery_flags = u32(payload, 408)
        profile_word = u32(payload, 464)
        integrity_word = u32(payload, 468)
        experiment_flags = (profile_word >> 24) & 0xFF
        result.update({
            "recovery_flags": recovery_flags,
            "recovery_ready": bool(recovery_flags & (1 << 0)),
            "previous_retained_boot_valid": bool(recovery_flags & (1 << 1)),
            "previous_boot_stable": bool(recovery_flags & (1 << 2)),
            "current_boot_stable": bool(recovery_flags & (1 << 3)),
            "wifi_quarantined": bool(recovery_flags & (1 << 4)),
            "wifi_start_allowed": bool(recovery_flags & (1 << 5)),
            "fallback_recovered": bool(recovery_flags & (1 << 6)),
            "source_changed": bool(recovery_flags & (1 << 7)),
            "build_changed": bool(recovery_flags & (1 << 8)),
            "retry_active": bool(recovery_flags & (1 << 9)),
            "firmware_source_id32": u32(payload, 412),
            "boot_sequence": u32(payload, 416),
            "consecutive_early_resets": u32(payload, 420),
            "early_reset_total": u32(payload, 424),
            "wifi_quarantine_total": u32(payload, 428),
            "previous_boot_sequence": u32(payload, 432),
            "previous_last_progress_id": u32(payload, 436),
            "previous_last_progress_detail": u32(payload, 440),
            "previous_last_progress_uptime_ms": u32(payload, 444),
            "current_last_progress_id": u32(payload, 448),
            "current_last_progress_detail": u32(payload, 452),
            "current_last_progress_uptime_ms": u32(payload, 456),
            "retained_event_sequence": u32(payload, 460),
            "reset_experiment_selector": profile_word & 0xFF,
            "requested_wifi_mode": (profile_word >> 8) & 0xFF,
            "effective_wifi_mode": (profile_word >> 16) & 0xFF,
            "reset_experiment_flags": experiment_flags,
            "watchdog_effective": bool(experiment_flags & (1 << 0)),
            "runtime_diagnostics_enabled": bool(experiment_flags & (1 << 1)),
            "application_can_data_tx_suppressed": bool(experiment_flags & (1 << 2)),
            "watchdog_requested": bool(experiment_flags & (1 << 3)),
            "watchdog_start_called": bool(experiment_flags & (1 << 4)),
            "watchdog_start_succeeded": bool(experiment_flags & (1 << 5)),
            "watchdog_timeout_matches": bool(experiment_flags & (1 << 6)),
            "retained_valid_events": integrity_word & 0xFFFF,
            "retained_corrupt_metadata": (integrity_word >> 16) & 0xFF,
            "retained_corrupt_events": (integrity_word >> 24) & 0xFF,
        })
    if len(payload) >= 508 and payload[52] >= 13:
        call_flags = u32(payload, 484)
        result.update({
            "runtime_contract_id32": u32(payload, 472),
            "recovery_identity_id32": u32(payload, 476),
            "watchdog_observed_timeout_ms": u32(payload, 480),
            "previous_wifi_call_flags": call_flags,
            "previous_wifi_call_valid": bool(call_flags & (1 << 0)),
            "previous_wifi_call_in_progress": bool(call_flags & (1 << 1)),
            "previous_wifi_call_completed": bool(call_flags & (1 << 2)),
            "previous_wifi_call_contract_changed": bool(call_flags & (1 << 3)),
            "previous_wifi_call_owner": (call_flags >> 8) & 0xFF,
            "previous_wifi_call_operation": (call_flags >> 16) & 0xFF,
            "previous_wifi_call_boot_sequence": u32(payload, 488),
            "previous_wifi_call_sequence": u32(payload, 492),
            "previous_wifi_call_started_ms": u32(payload, 496),
            "previous_wifi_call_duration_us": u32(payload, 500),
            "previous_wifi_call_result": i32(payload, 504),
        })
    return result


def decode_remote_state(payload):
    if len(payload) < 232:
        return None
    return {
        "mono_us": u64(payload, 0),
        "schema": payload[8],
        "link_state": payload[9],
        "flags": payload[12],
        "link_quality": payload[13],
        "m4_boot_id": u32(payload, 16),
        "shared_sequence": u32(payload, 20),
        "mailbox_age_ms": u32(payload, 24),
        "drive_permille": i16(payload, 28),
        "steering_permille": i16(payload, 30),
        "raw_ch2": u16(payload, 32),
        "raw_ch4": u16(payload, 34),
        "rx_bytes": u32(payload, 40),
        "accepted_rc_frames": u32(payload, 208),
        "candidate_updates": u32(payload, 84),
        "candidate_rejects": u32(payload, 88),
        "rejected_address": u32(payload, 92),
        "malformed_total": u32(payload, 96),
        "admission_resets": u32(payload, 100),
        "ipc_rejects": u32(payload, 104),
        "channel_valid_mask": u16(payload, 114),
        "admission_reject_detail": u16(payload, 116),
        "admission_streak": payload[118],
        "receiver_qualified": payload[119],
        "shared_publish_failures": u32(payload, 170),
        "last_rc_age_ms": u32(payload, 216),
        "last_link_statistics_age_ms": u32(payload, 220),
        "foreground_budget_hits": u32(payload, 228),
    }


def decode_capability(payload):
    if len(payload) < 192:
        return None
    return {
        "identity_schema": payload[112],
        "dirty": payload[113],
        "build_id": u32(payload, 120),
        "environment": zstr(payload, 144, 48),
    }


def decode_runtime_diagnostic(payload):
    if len(payload) != 128:
        return None
    fdcan_raw, fdcan = decode_fdcan_registers(payload, 40)
    stage_detail = u32(payload, 112)
    can_id_flags = u32(payload, 16)
    extended = bool(can_id_flags & (1 << 29))
    result = {
        "mono_us": u64(payload, 0),
        "schema": payload[8],
        "phase": payload[9],
        "phase_name": DIAGNOSTIC_PHASES.get(payload[9], f"unknown_{payload[9]}"),
        "boot_phase": payload[10],
        "flags": payload[11],
        "attempt_seq": u32(payload, 12),
        "can_id_flags": can_id_flags,
        "can_id": can_id_flags & (0x1FFFFFFF if extended else 0x7FF),
        "extended": extended,
        "rtr": bool(can_id_flags & (1 << 30)),
        "write_duration_us": u32(payload, 20),
        "write_rc": i32(payload, 24),
        "latest_tx_request_mask": u32(payload, 28),
        "hal_state": u32(payload, 32),
        "hal_error": u32(payload, 36),
        "before": fdcan,
        "after": fdcan,
        "before_raw": fdcan_raw,
        "after_raw": fdcan_raw,
        "boot_session_id": u64(payload, 104),
        "runtime_stage": stage_detail & 0xFF,
        "runtime_detail": (stage_detail >> 8) & 0xFF,
        "runtime_stage_flags": (stage_detail >> 16) & 0xFFFF,
        "wifi_worker_stack_free_bytes": u32(payload, 116),
        "wifi_worker_stack_max_used_bytes": u32(payload, 120),
        "build_id": u32(payload, 124),
    }
    wifi_flags = payload[73]
    result.update({
        "wifi_call_phase": payload[72],
        "wifi_call_phase_name": WIFI_CALL_PHASES.get(
            payload[72], f"unknown_{payload[72]}"
        ),
        "wifi_call_in_progress": bool(wifi_flags & (1 << 0)),
        "wifi_sink_connected": bool(wifi_flags & (1 << 1)),
        "wifi_worker_heartbeat_stale": bool(wifi_flags & (1 << 2)),
        "wifi_worker_running": bool(wifi_flags & (1 << 3)),
        "wifi_network_ready": bool(wifi_flags & (1 << 4)),
        "wifi_call_sequence": u32(payload, 76),
        "wifi_call_started_ms": u32(payload, 80),
        "wifi_call_duration_us": u32(payload, 84),
        "wifi_call_result": i32(payload, 88),
        "wifi_worker_heartbeat_age_ms": u32(payload, 92),
        "wifi_call_stall_total": u32(payload, 96),
        "wifi_connection_epoch": u32(payload, 100),
    })
    if payload[9] == 7:
        result.update({
            "recovery_event_type": payload[10],
            "recovery_event_sequence": u32(payload, 12),
            "recovery_event_boot_sequence": u32(payload, 16),
            "recovery_event_uptime_ms": u32(payload, 20),
            "recovery_event_value": u32(payload, 24),
            "recovery_event_code": u32(payload, 28),
            "recovery_event_identity_tag": u32(payload, 32),
        })
    elif payload[9] == 8:
        result.update({
            "recovered_wifi_call_owner": payload[10],
            "recovered_wifi_call_boot_sequence": u32(payload, 16),
            "recovered_wifi_call_completed_ms": u32(payload, 20),
            "recovered_wifi_call_contract_id": u64(payload, 32),
            "recovered_wifi_call_recovered": bool(wifi_flags & (1 << 5)),
            "recovered_wifi_call_completed": bool(wifi_flags & (1 << 6)),
            "recovered_wifi_call_contract_changed": bool(wifi_flags & (1 << 7)),
        })
    return result


class KvaserReader:
    def __init__(self, channel, event_queue):
        self.channel = channel
        self.event_queue = event_queue
        self.canlib = None
        self.handle = None
        self.channel_count = 0
        self.channel_name = None
        self.bus_on_ns = None
        self.stop_event = threading.Event()
        self.thread = None

    def status_text(self, status):
        buffer = ctypes.create_string_buffer(256)
        self.canlib.canGetErrorText(c_int(status), buffer, c_uint(len(buffer)))
        return buffer.value.decode(errors="replace")

    @staticmethod
    def _load_canlib():
        candidates = [pathlib.Path("canlib32.dll")]
        program_files = os.environ.get("ProgramFiles")
        program_files_x86 = os.environ.get("ProgramFiles(x86)")
        if program_files:
            candidates.append(
                pathlib.Path(program_files) / "Kvaser" / "Drivers" / "canlib32.dll"
            )
        if program_files_x86:
            candidates.append(
                pathlib.Path(program_files_x86) / "Kvaser" / "Drivers" / "canlib32.dll"
            )
        errors = []
        for candidate in candidates:
            try:
                return ctypes.WinDLL(str(candidate))
            except OSError as exc:
                errors.append(f"{candidate}: {exc}")
        raise RuntimeError("Kvaser CANlib DLL load failed: " + " | ".join(errors))

    def start(self):
        self.canlib = self._load_canlib()
        self.canlib.canInitializeLibrary()
        count = c_int()
        status = self.canlib.canGetNumberOfChannels(byref(count))
        if status != CAN_OK:
            raise RuntimeError(
                f"canGetNumberOfChannels failed {status}: {self.status_text(status)}"
            )
        self.channel_count = count.value
        if self.channel < 0 or self.channel >= self.channel_count:
            raise RuntimeError(
                f"Kvaser channel {self.channel} is outside 0..{self.channel_count - 1}"
            )

        name = ctypes.create_string_buffer(256)
        status = self.canlib.canGetChannelData(
            c_int(self.channel), c_int(CAN_CHANNELDATA_CHANNEL_NAME), name,
            c_size_t(len(name))
        )
        if status == CAN_OK:
            self.channel_name = name.value.decode(errors="replace")
        else:
            self.channel_name = f"channel_{self.channel}"
        if "virtual" in self.channel_name.casefold():
            raise RuntimeError(
                f"channel {self.channel} is virtual ({self.channel_name}); physical CAN is required"
            )

        # flags=0 is intentional: canOPEN_ACCEPT_VIRTUAL must never be used here.
        handle = self.canlib.canOpenChannel(c_int(self.channel), c_int(0))
        if handle < 0:
            raise RuntimeError(
                f"canOpenChannel failed {handle}: {self.status_text(handle)}"
            )
        self.handle = handle
        try:
            status = self.canlib.canSetBusParams(
                c_int(handle), c_long(CAN_BITRATE_500K),
                c_uint(0), c_uint(0), c_uint(0), c_uint(0), c_uint(0)
            )
            if status != CAN_OK:
                raise RuntimeError(
                    f"canSetBusParams failed {status}: {self.status_text(status)}"
                )
            status = self.canlib.canSetBusOutputControl(
                c_int(handle), c_uint(CAN_DRIVER_NORMAL)
            )
            if status != CAN_OK:
                raise RuntimeError(
                    f"canSetBusOutputControl(NORMAL) failed {status}: "
                    f"{self.status_text(status)}"
                )
            status = self.canlib.canBusOn(c_int(handle))
            if status != CAN_OK:
                raise RuntimeError(f"canBusOn failed {status}: {self.status_text(status)}")
            self.bus_on_ns = time.monotonic_ns()
            self.thread = threading.Thread(
                target=self._read_loop, name="kvaser-reader", daemon=True
            )
            self.thread.start()
        except Exception:
            self.canlib.canClose(c_int(handle))
            self.handle = None
            raise

    def _read_loop(self):
        while not self.stop_event.is_set():
            can_id = c_long()
            data_buffer = (c_ubyte * 8)()
            dlc = c_uint()
            flags = c_uint()
            timestamp = c_uint()
            status = self.canlib.canReadWait(
                c_int(self.handle), byref(can_id), data_buffer, byref(dlc),
                byref(flags), byref(timestamp), c_uint(50)
            )
            host_ns = time.monotonic_ns()
            if status == CAN_ERR_NOMSG:
                continue
            if status != CAN_OK:
                self.event_queue.put({
                    "host_monotonic_ns": host_ns,
                    "source": "kvaser",
                    "event": "read_error",
                    "status": status,
                    "status_text": self.status_text(status),
                })
                return
            length = min(dlc.value, 8)
            self.event_queue.put({
                "host_monotonic_ns": host_ns,
                "source": "kvaser",
                "event": "frame",
                "kvaser_timestamp_ms": timestamp.value,
                "can_id": can_id.value,
                "dlc": dlc.value,
                "flags": flags.value,
                "data": bytes(data_buffer[:length]).hex(),
            })

    def stop(self):
        self.stop_event.set()
        if self.thread is not None:
            self.thread.join(timeout=1.0)
        if self.handle is not None:
            self.canlib.canBusOff(c_int(self.handle))
            self.canlib.canClose(c_int(self.handle))
            self.handle = None


class Monitor:
    def __init__(self, args):
        self.args = args
        stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S_%f")
        self.run_dir = pathlib.Path(args.output_root).resolve() / f"monitor_remote_boot_hil_{stamp}"
        self.run_dir.mkdir(parents=True, exist_ok=False)
        self.timeline_handle = (self.run_dir / "timeline.jsonl").open(
            "w", encoding="utf-8", buffering=65536
        )
        self.kvaser_handle = (self.run_dir / "kvaser.jsonl").open(
            "w", encoding="utf-8", buffering=65536
        )
        self.evidence_handle = (self.run_dir / "evidence.jsonl").open(
            "w", encoding="utf-8", buffering=65536
        )
        self.event_queue = queue.Queue()
        self.kvaser = KvaserReader(args.channel, self.event_queue)
        self.started_utc = utc_now()
        self.started_ns = time.monotonic_ns()
        self.scope_start_ns = None
        self.capture_start_ns = None
        self.nominal_stop_ns = None
        self.fault_stop_ns = None
        self.interrupted = False
        self.exception = None
        self.completed_window = False

        self.serial_port = None
        self.serial_info = None
        self.raw_handle = None
        self.raw_paths = []
        self.usb_epoch_count = 0
        self.usb_disconnect_count = 0
        self.last_raw_flush_ns = self.started_ns
        self.last_artifact_flush_ns = self.started_ns
        self.parse_buffer = bytearray()
        self.epoch_has_frame = False
        self.typed_last_seq = None
        self.typed_bad_crc = 0
        self.typed_seq_gap_events = 0
        self.typed_seq_missing_total = 0
        self.typed_resync_discarded = 0
        self.record_counts = {}

        self.fault_counts = {}
        self.fault_examples = []
        self.kvaser_total_frames = 0
        self.kvaser_scope_frames = 0
        self.kvaser_error_frames = 0
        self.kvaser_read_errors = []
        self.control_frames = {can_id: [] for can_id in CONTROL_IDS}
        self.kvaser_control_sequence = []
        self.motor_rpm = []
        self.steering_deci_degree = []

        self.boot_session_ids = set()
        self.boot_session_sources = {}
        self.m4_boot_ids = set()
        self.build_ids = set()
        self.capability_environments = set()
        self.board_boot_events = 0
        self.health_records = []
        self.remote_records = []
        self.can_tx_raw_records = []
        self.phase5_records = []
        self.diagnostic_first = None
        self.diagnostic_last = None
        self.diagnostic_records = 0
        self.diagnostic_phase_counts = {}
        self.diagnostic_attempts = {}
        self.fdcan_fault_samples = 0
        self.fdcan_max_tec = 0
        self.fdcan_max_rec = 0
        self.fdcan_max_write_duration_us = 0
        self.fdcan_write_failures = 0
        self.fdcan_outcome_failures = 0
        self.usb_health_epochs = set()

        self.cold_armed = False
        self.cold_enumeration_observed = False
        self.initial_matches = []
        self.finalized = False
        self.manifest = {
            "schema": 1,
            "tool": "monitor_remote_boot_hil.py",
            "started_utc": self.started_utc,
            "started_monotonic_ns": self.started_ns,
            "command": sys.argv,
            "arguments": vars(args),
            "host": {
                "platform": platform.platform(),
                "python": sys.version,
                "machine": platform.machine(),
            },
            "usb_match": {
                "vid": f"0x{args.vid:04X}",
                "pid": f"0x{args.pid:04X}",
                "serial_number": args.serial,
                "input_buffer_reset": False,
            },
            "kvaser": {
                "channel": args.channel,
                "bitrate": 500000,
                "accept_virtual": False,
                "bus_on_before_usb": True,
            },
            "artifacts": {
                "timeline": "timeline.jsonl",
                "kvaser": "kvaser.jsonl",
                "evidence": "evidence.jsonl",
                "summary": "summary.json",
                "hashes": "SHA256SUMS.txt",
            },
        }
        self._write_manifest()

    def _write_manifest(self):
        (self.run_dir / "manifest.json").write_text(
            json.dumps(self.manifest, indent=2, ensure_ascii=False, sort_keys=True),
            encoding="utf-8",
        )

    def timeline(self, source, event, host_ns=None, **fields):
        value = {
            "host_monotonic_ns": host_ns if host_ns is not None else time.monotonic_ns(),
            "source": source,
            "event": event,
        }
        value.update(fields)
        json_line(self.timeline_handle, value)

    def _flush_artifacts(self, host_ns=None, force=False):
        now_ns = host_ns if host_ns is not None else time.monotonic_ns()
        if not force and now_ns - self.last_artifact_flush_ns < 250_000_000:
            return
        self.timeline_handle.flush()
        self.kvaser_handle.flush()
        self.evidence_handle.flush()
        if self.raw_handle is not None:
            self.raw_handle.flush()
        self.last_artifact_flush_ns = now_ns

    def evidence(self, record_type, seq, decoded, host_ns):
        json_line(self.evidence_handle, {
            "host_monotonic_ns": host_ns,
            "typed_seq": seq,
            "record_type": record_type,
            "record_name": TYPE_NAMES.get(record_type, f"type_{record_type}"),
            "decoded": decoded,
        })

    def fault(self, code, detail, host_ns=None):
        now_ns = host_ns if host_ns is not None else time.monotonic_ns()
        self.fault_counts[code] = self.fault_counts.get(code, 0) + 1
        if len(self.fault_examples) < 100:
            self.fault_examples.append({
                "host_monotonic_ns": now_ns,
                "code": code,
                "detail": detail,
            })
        if self.fault_counts[code] == 1:
            self.timeline("judge", "strict_fault", now_ns, code=code, detail=detail)
        if self.fault_stop_ns is None and not self.args.continue_after_fault:
            self.fault_stop_ns = now_ns + int(FAULT_TAIL_SECONDS * 1_000_000_000)
            self.timeline(
                "judge", "fault_tail_started", now_ns,
                tail_seconds=FAULT_TAIL_SECONDS,
                planned_stop_monotonic_ns=self.fault_stop_ns,
            )
        self._flush_artifacts(now_ns, force=True)

    def add_boot_session(self, value, source, host_ns):
        if value == 0:
            return
        self.boot_session_ids.add(value)
        self.boot_session_sources.setdefault(f"0x{value:016X}", set()).add(source)
        if len(self.boot_session_ids) > 1:
            self.fault(
                "m7_boot_session_changed",
                {"ids": [f"0x{x:016X}" for x in sorted(self.boot_session_ids)]},
                host_ns,
            )

    def _open_serial(self, port, host_ns):
        try:
            serial_port = serial.Serial(port.device, self.args.baud, timeout=0.02)
        except Exception as exc:
            self.fault(
                "usb_open_failed",
                {"device": port.device, "error": repr(exc)},
                host_ns,
            )
            return False
        self.serial_port = serial_port
        self.serial_info = port_description(port)
        self.usb_epoch_count += 1
        self.parse_buffer.clear()
        self.epoch_has_frame = False
        raw_name = f"usb_epoch_{self.usb_epoch_count:03d}_{safe_name(port.device)}.typed.bin"
        raw_path = self.run_dir / raw_name
        self.raw_handle = raw_path.open("wb")
        self.raw_paths.append(raw_name)
        self.timeline(
            "usb", "connected", host_ns,
            epoch=self.usb_epoch_count,
            identity=self.serial_info,
            raw_artifact=raw_name,
        )
        if self.usb_epoch_count > 1:
            self.fault(
                "usb_reenumeration",
                {"epoch": self.usb_epoch_count, "identity": self.serial_info},
                host_ns,
            )
        if self.capture_start_ns is None:
            self.capture_start_ns = host_ns
            self.nominal_stop_ns = host_ns + int(self.args.seconds * 1_000_000_000)
            self.timeline(
                "judge", "capture_window_started", host_ns,
                seconds=self.args.seconds,
                nominal_stop_monotonic_ns=self.nominal_stop_ns,
            )
        if self.args.cold_enumeration:
            self.cold_enumeration_observed = True
        return True

    def _close_serial(self, reason, host_ns, strict_disconnect):
        if self.serial_port is None:
            return
        identity = self.serial_info
        try:
            self.serial_port.close()
        except Exception:
            pass
        self.serial_port = None
        self.serial_info = None
        if self.raw_handle is not None:
            self.raw_handle.flush()
            self.raw_handle.close()
            self.raw_handle = None
        leftover = len(self.parse_buffer)
        self.parse_buffer.clear()
        self.timeline(
            "usb", "disconnected" if strict_disconnect else "closed",
            host_ns, epoch=self.usb_epoch_count, reason=reason,
            identity=identity, parser_leftover_bytes=leftover,
        )
        if strict_disconnect:
            self.usb_disconnect_count += 1
            self.fault(
                "usb_disconnect",
                {"epoch": self.usb_epoch_count, "reason": reason, "identity": identity},
                host_ns,
            )

    def _process_serial_chunk(self, chunk, host_ns):
        self.raw_handle.write(chunk)
        if host_ns - self.last_raw_flush_ns >= 1_000_000_000:
            self.raw_handle.flush()
            self.last_raw_flush_ns = host_ns
        self.parse_buffer.extend(chunk)
        while True:
            size_before = len(self.parse_buffer)
            frame = parse_frame(self.parse_buffer)
            size_after = len(self.parse_buffer)
            if frame is None:
                if size_after < size_before:
                    discarded = size_before - size_after
                    self.typed_resync_discarded += discarded
                    self.timeline(
                        "typed", "resync_discard", host_ns,
                        epoch=self.usb_epoch_count, bytes=discarded,
                        initial=not self.epoch_has_frame,
                    )
                    if self.epoch_has_frame:
                        self.fault(
                            "typed_resync_after_lock",
                            {"discarded": discarded, "epoch": self.usb_epoch_count},
                            host_ns,
                        )
                    continue
                break
            if frame.get("bad_crc"):
                self.typed_bad_crc += 1
                self.timeline(
                    "typed", "bad_crc", host_ns,
                    epoch=self.usb_epoch_count,
                    expected=frame["expected"], actual=frame["actual"],
                )
                self.fault(
                    "typed_crc_failure",
                    {"expected": frame["expected"], "actual": frame["actual"]},
                    host_ns,
                )
                continue
            self.epoch_has_frame = True
            self._process_typed_frame(frame, host_ns)

    def _process_typed_frame(self, frame, host_ns):
        seq = frame["seq"]
        if self.typed_last_seq is not None:
            expected = (self.typed_last_seq + 1) & 0xFFFF
            if seq != expected:
                missing = (seq - expected) & 0xFFFF
                self.typed_seq_gap_events += 1
                self.typed_seq_missing_total += missing
                detail = {
                    "previous": self.typed_last_seq,
                    "expected": expected,
                    "actual": seq,
                    "delta_modulo_65536": missing,
                    "epoch": self.usb_epoch_count,
                }
                self.timeline("typed", "sequence_gap", host_ns, **detail)
                self.fault("typed_sequence_gap", detail, host_ns)
        self.typed_last_seq = seq
        record_type = frame["type"]
        payload = frame["payload"]
        self.record_counts[record_type] = self.record_counts.get(record_type, 0) + 1
        self.timeline(
            "typed", "record", host_ns,
            epoch=self.usb_epoch_count,
            record_type=record_type,
            record_name=TYPE_NAMES.get(record_type, f"type_{record_type}"),
            seq=seq,
            flags=frame["flags"],
            payload_length=len(payload),
        )

        decoded = None
        if record_type == 2:
            decoded = decode_can_tx_raw(payload)
            if decoded is not None:
                captured = dict(decoded)
                captured["host_monotonic_ns"] = host_ns
                captured["typed_seq"] = seq
                captured["capture_index"] = len(self.can_tx_raw_records)
                self.can_tx_raw_records.append(captured)
            else:
                self.fault(
                    "can_tx_raw_bad_length",
                    {"minimum": 30, "actual": len(payload)}, host_ns,
                )
        elif record_type == 17:
            decoded = decode_stream_session(payload)
            if decoded is not None:
                self.add_boot_session(decoded["boot_session_id"], "STREAM_SESSION", host_ns)
                if (decoded["publish_seq64"] & 0xFFFF) != seq:
                    self.fault(
                        "stream_session_sequence_mismatch",
                        {"typed_seq": seq, "publish_seq64": decoded["publish_seq64"]},
                        host_ns,
                    )
        elif record_type == 8:
            decoded = decode_board_health(payload)
            if decoded is not None:
                self.health_records.append(decoded)
                if "boot_session_id" in decoded:
                    self.add_boot_session(decoded["boot_session_id"], "BOARD_HEALTH", host_ns)
                if "usb_connection_epoch" in decoded:
                    self.usb_health_epochs.add(decoded["usb_connection_epoch"])
                    if len(self.usb_health_epochs) > 1:
                        self.fault(
                            "firmware_usb_epoch_changed",
                            {"epochs": sorted(self.usb_health_epochs)}, host_ns,
                        )
        elif record_type == 7:
            decoded = decode_board_event(payload)
            if decoded is not None:
                if decoded["code"] == 1:
                    self.board_boot_events += 1
                    if self.board_boot_events > 1:
                        self.fault(
                            "multiple_board_boot_events",
                            {"count": self.board_boot_events}, host_ns,
                        )
                if decoded["code"] in STRICT_BOARD_EVENTS:
                    self.fault("board_fault_event", decoded, host_ns)
        elif record_type == 18:
            decoded = decode_remote_state(payload)
            if decoded is None:
                self.fault(
                    "remote_control_state_bad_length",
                    {"expected": 232, "actual": len(payload)}, host_ns,
                )
            else:
                self.remote_records.append(decoded)
                if decoded["schema"] != 4:
                    self.fault(
                        "remote_control_state_schema_mismatch",
                        {"expected": 4, "actual": decoded["schema"]}, host_ns,
                    )
                m4_boot_id = decoded["m4_boot_id"]
                if m4_boot_id != 0:
                    self.m4_boot_ids.add(m4_boot_id)
                    if len(self.m4_boot_ids) > 1:
                        self.fault(
                            "m4_boot_id_changed",
                            {"ids": [f"0x{x:08X}" for x in sorted(self.m4_boot_ids)]},
                            host_ns,
                        )
                if len(self.remote_records) >= 2:
                    previous = self.remote_records[-2]
                    for field in (
                        "malformed_total", "ipc_rejects", "shared_publish_failures",
                    ):
                        delta = counter_delta(previous[field], decoded[field])
                        if delta:
                            self.fault(
                                f"remote_{field}_advanced",
                                {"delta": delta, "value": decoded[field]}, host_ns,
                            )
        elif record_type == 19:
            decoded = decode_runtime_diagnostic(payload)
            if decoded is None:
                self.fault(
                    "runtime_diagnostic_bad_length",
                    {"expected": 128, "actual": len(payload)}, host_ns,
                )
            else:
                self._process_runtime_diagnostic(decoded, host_ns)
        elif record_type == 9:
            decoded = decode_capability(payload)
            if decoded is not None:
                self.capability_environments.add(decoded["environment"])
                if decoded["build_id"] != 0:
                    self.build_ids.add(decoded["build_id"])

        if decoded is not None and record_type in TYPE_NAMES:
            self.evidence(record_type, seq, decoded, host_ns)

    def _process_runtime_diagnostic(self, decoded, host_ns):
        self.diagnostic_records += 1
        self.diagnostic_first = self.diagnostic_first or decoded
        self.diagnostic_last = decoded
        phase = decoded["phase"]
        self.diagnostic_phase_counts[phase] = self.diagnostic_phase_counts.get(phase, 0) + 1
        if phase == 5:
            captured = dict(decoded)
            captured["host_monotonic_ns"] = host_ns
            captured["capture_index"] = len(self.phase5_records)
            self.phase5_records.append(captured)
        self.add_boot_session(decoded["boot_session_id"], "RUNTIME_DIAGNOSTIC", host_ns)
        if decoded["build_id"] != 0:
            self.build_ids.add(decoded["build_id"])
            if len(self.build_ids) > 1:
                self.fault(
                    "firmware_build_id_changed",
                    {"ids": [f"0x{x:08X}" for x in sorted(self.build_ids)]}, host_ns,
                )
        if decoded["schema"] != 2:
            self.fault(
                "runtime_diagnostic_schema_mismatch",
                {"expected": 2, "actual": decoded["schema"]}, host_ns,
            )
        if decoded["hal_error"] != 0:
            self.fault(
                "fdcan_hal_error",
                {"attempt_seq": decoded["attempt_seq"],
                 "hal_error": f"0x{decoded['hal_error']:08X}"}, host_ns,
            )

        for boundary in ("before",):
            state = decoded[boundary]
            self.fdcan_max_tec = max(self.fdcan_max_tec, state["tec"])
            self.fdcan_max_rec = max(self.fdcan_max_rec, state["rec"])
            if state["faults"]:
                self.fdcan_fault_samples += 1
                self.fault(
                    "fdcan_protocol_state",
                    {
                        "attempt_seq": decoded["attempt_seq"],
                        "phase": decoded["phase_name"],
                        "boundary": boundary,
                        "faults": state["faults"],
                        "registers": state["raw"],
                    },
                    host_ns,
                )

        attempt_seq = decoded["attempt_seq"]
        if attempt_seq != 0:
            attempt = self.diagnostic_attempts.setdefault(attempt_seq, {})
            attempt[phase] = decoded
        if phase in (4, 5):
            self.fdcan_max_write_duration_us = max(
                self.fdcan_max_write_duration_us, decoded["write_duration_us"]
            )
            if decoded["write_rc"] != 1:
                self.fdcan_write_failures += 1
                self.fault(
                    "fdcan_write_rejected",
                    {"attempt_seq": attempt_seq, "rc": decoded["write_rc"],
                     "phase": decoded["phase_name"]}, host_ns,
                )
            if decoded["write_duration_us"] > self.args.fdcan_write_budget_us:
                self.fault(
                    "fdcan_write_budget_exceeded",
                    {"attempt_seq": attempt_seq,
                     "duration_us": decoded["write_duration_us"],
                     "budget_us": self.args.fdcan_write_budget_us}, host_ns,
                )
        if phase == 5:
            request_mask = decoded["latest_tx_request_mask"]
            outcome_faults = []
            if (
                request_mask == 0
                or request_mask & ~0x7
                or request_mask & (request_mask - 1)
            ):
                outcome_faults.append(f"invalid_request_mask=0x{request_mask:08X}")
            else:
                after_raw = decoded["after_raw"]
                if after_raw["TXBTO"] & request_mask != request_mask:
                    outcome_faults.append("TXBTO_not_set")
                if after_raw["TXBRP"] & request_mask:
                    outcome_faults.append("TXBRP_still_pending")
                if after_raw["TXBCF"] & request_mask:
                    outcome_faults.append("TXBCF_cancelled")
            if outcome_faults:
                self.fdcan_outcome_failures += 1
                self.fault(
                    "fdcan_tx_outcome_invalid",
                    {"attempt_seq": attempt_seq, "faults": outcome_faults,
                     "latest_tx_request_mask": f"0x{request_mask:08X}",
                     "after": decoded["after"]["raw"]}, host_ns,
                )

    def _drain_kvaser(self):
        while True:
            try:
                event = self.event_queue.get_nowait()
            except queue.Empty:
                return
            host_ns = event["host_monotonic_ns"]
            in_scope = self.scope_start_ns is not None and host_ns >= self.scope_start_ns
            event["in_scope"] = in_scope
            is_control = (
                event["event"] == "frame"
                and in_scope
                and not (event["flags"] & CANMSG_ERROR_FRAME)
                and event["can_id"] in self.control_frames
            )
            if is_control:
                event["control_sequence_index"] = len(self.kvaser_control_sequence)
            json_line(self.kvaser_handle, event)
            self.timeline_handle.write(json.dumps(event, ensure_ascii=False, sort_keys=True) + "\n")
            if event["event"] == "read_error":
                self.kvaser_read_errors.append(event)
                self.fault("kvaser_read_error", event, host_ns)
                continue
            self.kvaser_total_frames += 1
            if not in_scope:
                continue
            self.kvaser_scope_frames += 1
            if event["flags"] & CANMSG_ERROR_FRAME:
                self.kvaser_error_frames += 1
                self.fault(
                    "kvaser_error_frame",
                    {"count": self.kvaser_error_frames,
                     "timestamp_ms": event["kvaser_timestamp_ms"],
                     "flags": f"0x{event['flags']:X}"}, host_ns,
                )
                continue
            can_id = event["can_id"]
            if can_id in self.control_frames:
                self.kvaser_control_sequence.append(event)
                self.control_frames[can_id].append(event)
                if can_id == 0x510:
                    data = bytes.fromhex(event["data"])
                    if len(data) >= 5:
                        self.motor_rpm.append(int.from_bytes(data[1:3], "little", signed=True))
                        self.steering_deci_degree.append(
                            int.from_bytes(data[3:5], "little", signed=True)
                        )

    def run(self):
        print(f"artifacts={self.run_dir}")
        print("Starting physical Kvaser bus before USB observation...")
        self.kvaser.start()
        self.manifest["kvaser"].update({
            "channel_count": self.kvaser.channel_count,
            "channel_name": self.kvaser.channel_name,
            "bus_on_monotonic_ns": self.kvaser.bus_on_ns,
        })
        self._write_manifest()
        self.timeline(
            "kvaser", "bus_on", self.kvaser.bus_on_ns,
            channel=self.args.channel, channel_name=self.kvaser.channel_name,
            bitrate=500000, accept_virtual=False,
        )

        self.initial_matches = matching_ports(self.args.vid, self.args.pid, self.args.serial)
        self.manifest["usb_match"]["initial_matches"] = [
            port_description(item) for item in self.initial_matches
        ]
        self._write_manifest()
        enumeration_deadline_ns = time.monotonic_ns() + int(
            self.args.enumeration_timeout * 1_000_000_000
        )
        if self.args.cold_enumeration:
            if self.initial_matches:
                print("Cold enumeration armed: disconnect/reset target; waiting for absence then appearance.")
                self.timeline(
                    "usb", "cold_waiting_for_absence",
                    matches=[port_description(item) for item in self.initial_matches],
                )
            else:
                self.cold_armed = True
                self.scope_start_ns = time.monotonic_ns()
                print("Cold enumeration armed: target absent; waiting for appearance.")
                self.timeline("usb", "cold_absence_observed", self.scope_start_ns)
        else:
            self.scope_start_ns = self.kvaser.bus_on_ns

        next_pnp_poll_ns = 0
        while True:
            now_ns = time.monotonic_ns()
            self._drain_kvaser()

            if now_ns >= next_pnp_poll_ns:
                matches = matching_ports(self.args.vid, self.args.pid, self.args.serial)
                if self.args.cold_enumeration and not self.cold_armed and not matches:
                    self.cold_armed = True
                    self.scope_start_ns = now_ns
                    self.timeline("usb", "cold_absence_observed", now_ns)
                    print("Target absence observed; waiting for cold enumeration.")
                if len(matches) > 1:
                    self.fault(
                        "usb_identity_ambiguous",
                        {"matches": [port_description(item) for item in matches]}, now_ns,
                    )
                elif self.serial_port is not None:
                    devices = {item.device.casefold() for item in matches}
                    if self.serial_info["device"].casefold() not in devices:
                        self._close_serial(
                            "PnP target disappeared", now_ns, strict_disconnect=True
                        )
                elif (not self.args.cold_enumeration or self.cold_armed) and matches:
                    if self._open_serial(matches[0], now_ns):
                        print(f"USB epoch {self.usb_epoch_count}: {matches[0].device}")
                next_pnp_poll_ns = now_ns + 100_000_000

            if self.serial_port is not None:
                try:
                    chunk = self.serial_port.read(8192)
                except (serial.SerialException, OSError) as exc:
                    self._close_serial(
                        f"read failed: {exc!r}", time.monotonic_ns(), strict_disconnect=True
                    )
                else:
                    if chunk:
                        self._process_serial_chunk(chunk, time.monotonic_ns())
            else:
                time.sleep(0.01)

            now_ns = time.monotonic_ns()
            self._flush_artifacts(now_ns)
            if self.capture_start_ns is None and now_ns >= enumeration_deadline_ns:
                self.fault(
                    "usb_enumeration_timeout",
                    {"timeout_seconds": self.args.enumeration_timeout,
                     "cold_enumeration": self.args.cold_enumeration}, now_ns,
                )
                break
            if self.fault_stop_ns is not None and now_ns >= self.fault_stop_ns:
                break
            if (
                self.nominal_stop_ns is not None
                and self.fault_stop_ns is None
                and now_ns >= self.nominal_stop_ns
            ):
                self.completed_window = True
                break

    def _correlate_can_evidence(self):
        window_ns = int(self.args.correlation_window_ms * 1_000_000)
        future_slack_ns = int(CORRELATION_FUTURE_SLACK_MS * 1_000_000)
        raw_control = sorted(
            (
                item for item in self.can_tx_raw_records
                if item["can_id"] in CONTROL_IDS
                and item["bus"] == 1
                and not item["extended"]
                and not item["rtr"]
            ),
            key=lambda item: (item["mono_us"], item["tx_total"], item["capture_index"]),
        )
        for index, item in enumerate(raw_control):
            item["correlation_index"] = index
        physical = list(self.kvaser_control_sequence)
        phase_by_attempt = {}
        duplicate_phase5_attempts = 0
        for item in self.phase5_records:
            attempt_seq = item["attempt_seq"]
            if attempt_seq in phase_by_attempt:
                duplicate_phase5_attempts += 1
            else:
                phase_by_attempt[attempt_seq] = item
        phase5 = sorted(
            phase_by_attempt.values(),
            key=lambda item: (item["attempt_seq"], item["mono_us"], item["capture_index"]),
        )

        raw_bursts = extract_control_bursts(raw_control)
        physical_bursts = extract_control_bursts(physical)
        physical_bursts_by_signature = {}
        for index, burst in enumerate(physical_bursts["complete"]):
            burst["burst_index"] = index
            physical_bursts_by_signature.setdefault(burst["signature"], []).append(burst)

        preassigned_raw_to_physical = {}
        used_physical_bursts = set()
        burst_match_counts = {"unique": 0, "ambiguous": 0, "unmatched": 0}
        for raw_burst in raw_bursts["complete"]:
            typed_host_ns = raw_burst["frames"][0]["host_monotonic_ns"]
            candidates = []
            for physical_burst in physical_bursts_by_signature.get(
                raw_burst["signature"], []
            ):
                burst_index = physical_burst["burst_index"]
                if burst_index in used_physical_bursts:
                    continue
                physical_host_ns = physical_burst["frames"][0]["host_monotonic_ns"]
                source_delta_ns = typed_host_ns - physical_host_ns
                frame_deltas = [
                    raw_frame["host_monotonic_ns"]
                    - physical_frame["host_monotonic_ns"]
                    for raw_frame, physical_frame in zip(
                        raw_burst["frames"], physical_burst["frames"]
                    )
                ]
                if (
                    -future_slack_ns <= source_delta_ns <= window_ns
                    and all(
                        -future_slack_ns <= delta <= window_ns
                        for delta in frame_deltas
                    )
                ):
                    candidates.append((physical_burst, source_delta_ns))
            if len(candidates) == 1:
                physical_burst, source_delta_ns = candidates[0]
                used_physical_bursts.add(physical_burst["burst_index"])
                burst_match_counts["unique"] += 1
                for raw_frame, physical_frame in zip(
                    raw_burst["frames"], physical_burst["frames"]
                ):
                    preassigned_raw_to_physical[raw_frame["correlation_index"]] = {
                        "physical": physical_frame,
                        "basis": "complete_burst_id_data_order",
                        "source_delta_ns": (
                            raw_frame["host_monotonic_ns"]
                            - physical_frame["host_monotonic_ns"]
                        ),
                    }
            elif len(candidates) > 1:
                burst_match_counts["ambiguous"] += 1
            else:
                burst_match_counts["unmatched"] += 1

        physical_by_id = {can_id: [] for can_id in CONTROL_IDS}
        physical_times_by_id = {can_id: [] for can_id in CONTROL_IDS}
        for item in physical:
            physical_by_id[item["can_id"]].append(item)
        for can_id in CONTROL_IDS:
            physical_times_by_id[can_id] = [
                item["host_monotonic_ns"] for item in physical_by_id[can_id]
            ]

        used_physical_indices = {
            match["physical"]["control_sequence_index"]
            for match in preassigned_raw_to_physical.values()
        }
        raw_physical_results = []
        raw_physical_by_index = {}
        for raw in raw_control:
            raw_index = raw["correlation_index"]
            preassigned = preassigned_raw_to_physical.get(raw_index)
            if preassigned is not None:
                physical_frame = preassigned["physical"]
                result = {
                    "raw_index": raw_index,
                    "typed_seq": raw["typed_seq"],
                    "can_id": f"0x{raw['can_id']:X}",
                    "data": raw["data"],
                    "status": "unique",
                    "basis": preassigned["basis"],
                    "candidate_count": 1,
                    "physical_control_index": physical_frame["control_sequence_index"],
                    "physical_host_monotonic_ns": physical_frame["host_monotonic_ns"],
                    "typed_host_monotonic_ns": raw["host_monotonic_ns"],
                    "physical_to_typed_delta_ms": preassigned["source_delta_ns"] / 1e6,
                }
            else:
                can_id = raw["can_id"]
                values = physical_by_id[can_id]
                times = physical_times_by_id[can_id]
                lower = raw["host_monotonic_ns"] - window_ns
                upper = raw["host_monotonic_ns"] + future_slack_ns
                begin = bisect.bisect_left(times, lower)
                end = bisect.bisect_right(times, upper)
                candidates = [
                    item for item in values[begin:end]
                    if item["control_sequence_index"] not in used_physical_indices
                    and item["dlc"] == raw["dlc"]
                    and item["data"] == raw["data"]
                ]
                if len(candidates) == 1:
                    physical_frame = candidates[0]
                    used_physical_indices.add(physical_frame["control_sequence_index"])
                    result = {
                        "raw_index": raw_index,
                        "typed_seq": raw["typed_seq"],
                        "can_id": f"0x{can_id:X}",
                        "data": raw["data"],
                        "status": "unique",
                        "basis": "bounded_host_window_id_data",
                        "candidate_count": 1,
                        "physical_control_index": physical_frame["control_sequence_index"],
                        "physical_host_monotonic_ns": physical_frame["host_monotonic_ns"],
                        "typed_host_monotonic_ns": raw["host_monotonic_ns"],
                        "physical_to_typed_delta_ms": (
                            raw["host_monotonic_ns"]
                            - physical_frame["host_monotonic_ns"]
                        ) / 1e6,
                    }
                else:
                    result = {
                        "raw_index": raw_index,
                        "typed_seq": raw["typed_seq"],
                        "can_id": f"0x{can_id:X}",
                        "data": raw["data"],
                        "status": "ambiguous" if candidates else "unmatched",
                        "basis": "bounded_host_window_id_data",
                        "candidate_count": len(candidates),
                        "candidate_control_indices": [
                            item["control_sequence_index"] for item in candidates[:20]
                        ],
                        "typed_host_monotonic_ns": raw["host_monotonic_ns"],
                    }
            raw_physical_results.append(result)
            raw_physical_by_index[raw_index] = result

        raw_by_id = {can_id: [] for can_id in CONTROL_IDS}
        raw_mono_by_id = {can_id: [] for can_id in CONTROL_IDS}
        for item in raw_control:
            raw_by_id[item["can_id"]].append(item)
        for can_id in CONTROL_IDS:
            raw_mono_by_id[can_id] = [item["mono_us"] for item in raw_by_id[can_id]]

        used_raw_indices = set()
        phase_raw_results = []
        phase_raw_match = {}
        for phase in phase5:
            can_id = phase["can_id"]
            values = raw_by_id.get(can_id, [])
            times = raw_mono_by_id.get(can_id, [])
            lower_mono = max(0, phase["mono_us"] - RAW_TO_OUTCOME_MAX_US)
            begin = bisect.bisect_left(times, lower_mono)
            end = bisect.bisect_right(times, phase["mono_us"])
            candidates = [
                item for item in values[begin:end]
                if item["correlation_index"] not in used_raw_indices
                and abs(item["host_monotonic_ns"] - phase["host_monotonic_ns"])
                <= window_ns
            ]
            if len(candidates) == 1:
                raw = candidates[0]
                used_raw_indices.add(raw["correlation_index"])
                phase_raw_match[phase["attempt_seq"]] = raw
                result = {
                    "attempt_seq": phase["attempt_seq"],
                    "can_id": f"0x{can_id:X}",
                    "status": "unique",
                    "basis": "same_id_prior_board_mono_and_bounded_host_window",
                    "candidate_count": 1,
                    "raw_index": raw["correlation_index"],
                    "can_tx_raw_typed_seq": raw["typed_seq"],
                    "board_raw_to_outcome_delta_us": phase["mono_us"] - raw["mono_us"],
                    "typed_host_delta_ms": (
                        phase["host_monotonic_ns"] - raw["host_monotonic_ns"]
                    ) / 1e6,
                }
            else:
                result = {
                    "attempt_seq": phase["attempt_seq"],
                    "can_id": f"0x{can_id:X}",
                    "status": "ambiguous" if candidates else "unmatched",
                    "basis": "same_id_prior_board_mono_and_bounded_host_window",
                    "candidate_count": len(candidates),
                    "candidate_raw_indices": [
                        item["correlation_index"] for item in candidates[:20]
                    ],
                }
            phase_raw_results.append(result)

        phase_physical_results = []
        phase_used_physical = set()
        for phase in phase5:
            attempt_seq = phase["attempt_seq"]
            raw = phase_raw_match.get(attempt_seq)
            if raw is not None:
                raw_result = raw_physical_by_index[raw["correlation_index"]]
                if raw_result["status"] == "unique":
                    physical_index = raw_result["physical_control_index"]
                    phase_source_delta_ns = (
                        phase["host_monotonic_ns"]
                        - raw_result["physical_host_monotonic_ns"]
                    )
                    if not (
                        -future_slack_ns <= phase_source_delta_ns <= window_ns
                    ):
                        status = "out_of_window"
                        basis = "CAN_TX_RAW_data_match_outside_phase5_host_window"
                    elif physical_index in phase_used_physical:
                        status = "ambiguous"
                        basis = "physical_frame_already_claimed"
                    else:
                        phase_used_physical.add(physical_index)
                        status = "unique"
                        basis = f"can_tx_raw_{raw_result['basis']}"
                    result = {
                        "attempt_seq": attempt_seq,
                        "can_id": f"0x{phase['can_id']:X}",
                        "status": status,
                        "basis": basis,
                        "data_confirmed": status == "unique",
                        "raw_index": raw["correlation_index"],
                        "physical_control_index": physical_index,
                        "physical_to_phase_host_delta_ms": phase_source_delta_ns / 1e6,
                    }
                else:
                    result = {
                        "attempt_seq": attempt_seq,
                        "can_id": f"0x{phase['can_id']:X}",
                        "status": raw_result["status"],
                        "basis": "CAN_TX_RAW_present_but_physical_match_not_unique",
                        "data_confirmed": False,
                        "raw_index": raw["correlation_index"],
                        "raw_physical_candidate_count": raw_result["candidate_count"],
                    }
            else:
                can_id = phase["can_id"]
                values = physical_by_id.get(can_id, [])
                times = physical_times_by_id.get(can_id, [])
                lower = phase["host_monotonic_ns"] - window_ns
                upper = phase["host_monotonic_ns"] + future_slack_ns
                begin = bisect.bisect_left(times, lower)
                end = bisect.bisect_right(times, upper)
                candidates = [
                    item for item in values[begin:end]
                    if item["control_sequence_index"] not in phase_used_physical
                    and item["control_sequence_index"] not in used_physical_indices
                ]
                if len(candidates) == 1:
                    physical_frame = candidates[0]
                    phase_used_physical.add(physical_frame["control_sequence_index"])
                    result = {
                        "attempt_seq": attempt_seq,
                        "can_id": f"0x{can_id:X}",
                        "status": "unique_id_only",
                        "basis": "bounded_host_window_id_only_no_CAN_TX_RAW",
                        "data_confirmed": False,
                        "physical_control_index": physical_frame["control_sequence_index"],
                        "physical_to_phase_host_delta_ms": (
                            phase["host_monotonic_ns"]
                            - physical_frame["host_monotonic_ns"]
                        ) / 1e6,
                    }
                else:
                    result = {
                        "attempt_seq": attempt_seq,
                        "can_id": f"0x{can_id:X}",
                        "status": "ambiguous" if candidates else "unmatched",
                        "basis": "bounded_host_window_id_only_no_CAN_TX_RAW",
                        "data_confirmed": False,
                        "candidate_count": len(candidates),
                        "candidate_control_indices": [
                            item["control_sequence_index"] for item in candidates[:20]
                        ],
                    }
            phase_physical_results.append(result)

        def count_status(results):
            counts = {}
            for result in results:
                status = result["status"]
                counts[status] = counts.get(status, 0) + 1
            return counts

        raw_status_counts = count_status(raw_physical_results)
        phase_raw_status_counts = count_status(phase_raw_results)
        phase_physical_status_counts = count_status(phase_physical_results)
        data_confirmed = sum(
            1 for item in phase_physical_results if item.get("data_confirmed")
        )
        for item in raw_physical_results:
            json_line(self.evidence_handle, {
                "evidence_kind": "CAN_TX_RAW_TO_KVASER_CORRELATION",
                **item,
            })
        for raw_result, physical_result in zip(
            phase_raw_results, phase_physical_results
        ):
            json_line(self.evidence_handle, {
                "evidence_kind": "PHASE5_CAN_CORRELATION",
                "phase5_to_can_tx_raw": raw_result,
                "phase5_to_kvaser": physical_result,
            })

        raw_burst_summary = {
            key: value for key, value in raw_bursts.items() if key != "complete"
        }
        physical_burst_summary = {
            key: value for key, value in physical_bursts.items() if key != "complete"
        }
        raw_rolling = analyze_rolling_nibble(raw_control)
        physical_rolling = analyze_rolling_nibble(physical)
        summary = {
            "host_window_ms": self.args.correlation_window_ms,
            "future_source_order_slack_ms": CORRELATION_FUTURE_SLACK_MS,
            "raw_to_outcome_board_window_us": RAW_TO_OUTCOME_MAX_US,
            "counts": {
                "phase5_records": len(self.phase5_records),
                "phase5_unique_attempts": len(phase5),
                "phase5_duplicate_attempt_records": duplicate_phase5_attempts,
                "can_tx_raw_records": len(self.can_tx_raw_records),
                "can_tx_raw_control_records": len(raw_control),
                "kvaser_control_frames": len(physical),
                "phase5_to_can_tx_raw": phase_raw_status_counts,
                "can_tx_raw_to_kvaser": raw_status_counts,
                "phase5_to_kvaser": phase_physical_status_counts,
                "phase5_kvaser_data_confirmed": data_confirmed,
                "burst_matches": burst_match_counts,
            },
            "burst_order": {
                "can_tx_raw": raw_burst_summary,
                "kvaser": physical_burst_summary,
            },
            "rolling_nibble_0x503": {
                "can_tx_raw": raw_rolling,
                "kvaser": physical_rolling,
            },
            "unmatched_or_ambiguous_examples": {
                "phase5_to_can_tx_raw": [
                    item for item in phase_raw_results if item["status"] != "unique"
                ][:50],
                "can_tx_raw_to_kvaser": [
                    item for item in raw_physical_results if item["status"] != "unique"
                ][:50],
                "phase5_to_kvaser": [
                    item for item in phase_physical_results
                    if item["status"] not in ("unique", "unique_id_only")
                ][:50],
            },
        }
        self.timeline("judge", "can_evidence_correlation_complete", summary=summary)
        return summary

    def _checks_and_summary(self):
        correlation = self._correlate_can_evidence()
        checks = []

        def check(name, passed, detail):
            checks.append({"name": name, "pass": bool(passed), "detail": detail})

        check(
            "capture_window",
            self.capture_start_ns is not None,
            {"capture_started": self.capture_start_ns is not None,
             "completed_without_early_fault": self.completed_window},
        )
        if self.args.cold_enumeration:
            check(
                "cold_enumeration",
                self.cold_enumeration_observed,
                {"armed": self.cold_armed, "observed": self.cold_enumeration_observed},
            )
        check(
            "usb_single_epoch",
            self.usb_epoch_count == 1 and self.usb_disconnect_count == 0,
            {"epochs": self.usb_epoch_count, "disconnects": self.usb_disconnect_count},
        )
        check(
            "typed_crc",
            self.typed_bad_crc == 0,
            {"bad_crc": self.typed_bad_crc},
        )
        check(
            "typed_sequence",
            self.typed_seq_gap_events == 0,
            {"gap_events": self.typed_seq_gap_events,
             "missing_modulo_total": self.typed_seq_missing_total},
        )
        check(
            "stream_session_present",
            self.record_counts.get(17, 0) >= 1,
            {"records": self.record_counts.get(17, 0)},
        )
        check(
            "board_health_progress",
            len(self.health_records) >= 2,
            {"records": len(self.health_records)},
        )
        check(
            "remote_state_progress",
            len(self.remote_records) >= 2,
            {"records": len(self.remote_records)},
        )
        check(
            "kvaser_read",
            not self.kvaser_read_errors,
            {"read_errors": self.kvaser_read_errors},
        )
        check(
            "kvaser_error_frames",
            self.kvaser_error_frames == 0,
            {"error_frames": self.kvaser_error_frames},
        )
        check(
            "runtime_diagnostic_present",
            self.diagnostic_records > 0,
            {"records": self.diagnostic_records,
             "phase_counts": {DIAGNOSTIC_PHASES.get(key, str(key)): value
                              for key, value in sorted(self.diagnostic_phase_counts.items())}},
        )
        check(
            "fdcan_protocol_state",
            self.fdcan_fault_samples == 0,
            {"fault_samples": self.fdcan_fault_samples,
             "max_tec": self.fdcan_max_tec, "max_rec": self.fdcan_max_rec},
        )
        check(
            "fdcan_write",
            self.fdcan_write_failures == 0
            and self.fdcan_max_write_duration_us <= self.args.fdcan_write_budget_us,
            {"write_failures": self.fdcan_write_failures,
             "max_duration_us": self.fdcan_max_write_duration_us,
             "budget_us": self.args.fdcan_write_budget_us},
        )
        outcome_count = self.diagnostic_phase_counts.get(5, 0)
        check(
            "fdcan_tx_outcome",
            outcome_count > 0 and self.fdcan_outcome_failures == 0,
            {"outcome_records": outcome_count,
             "outcome_failures": self.fdcan_outcome_failures},
        )
        correlation_counts = correlation["counts"]
        phase_attempts = correlation_counts["phase5_unique_attempts"]
        phase_raw_counts = correlation_counts["phase5_to_can_tx_raw"]
        raw_physical_counts = correlation_counts["can_tx_raw_to_kvaser"]
        phase_physical_counts = correlation_counts["phase5_to_kvaser"]
        check(
            "phase5_CAN_TX_RAW_correlation",
            phase_attempts > 0
            and correlation_counts["phase5_duplicate_attempt_records"] == 0
            and phase_raw_counts.get("unique", 0) == phase_attempts,
            {
                "phase5_records": correlation_counts["phase5_records"],
                "unique_attempts": phase_attempts,
                "duplicate_attempt_records": correlation_counts[
                    "phase5_duplicate_attempt_records"
                ],
                "match_status": phase_raw_counts,
            },
        )
        raw_control_count = correlation_counts["can_tx_raw_control_records"]
        check(
            "CAN_TX_RAW_kvaser_correlation",
            raw_control_count > 0
            and raw_physical_counts.get("unique", 0) == raw_control_count,
            {
                "can_tx_raw_control_records": raw_control_count,
                "match_status": raw_physical_counts,
                "host_window_ms": correlation["host_window_ms"],
            },
        )
        check(
            "phase5_physical_kvaser_correlation",
            phase_attempts > 0
            and correlation_counts["phase5_kvaser_data_confirmed"] == phase_attempts
            and phase_physical_counts.get("unique", 0) == phase_attempts,
            {
                "phase5_attempts": phase_attempts,
                "data_confirmed": correlation_counts[
                    "phase5_kvaser_data_confirmed"
                ],
                "match_status": phase_physical_counts,
                "note": "ID-only unique matches are reported but are not data-confirmed proof",
            },
        )
        raw_burst = correlation["burst_order"]["can_tx_raw"]
        physical_burst = correlation["burst_order"]["kvaser"]
        check(
            "complete_5_id_burst_order",
            raw_burst["complete_count"] > 0
            and physical_burst["complete_count"] > 0
            and raw_burst["non_trailing_incomplete_count"] == 0
            and physical_burst["non_trailing_incomplete_count"] == 0,
            {"expected_ids": [f"0x{value:X}" for value in CONTROL_IDS],
             "can_tx_raw": raw_burst, "kvaser": physical_burst},
        )
        raw_rolling = correlation["rolling_nibble_0x503"]["can_tx_raw"]
        physical_rolling = correlation["rolling_nibble_0x503"]["kvaser"]
        rolling_ok = True
        for rolling in (raw_rolling, physical_rolling):
            if rolling["applicable"]:
                rolling_ok = rolling_ok and rolling["checksum_failures"] == 0
                rolling_ok = rolling_ok and rolling["continuity_failures"] == 0
        check(
            "rolling_nibble_0x503_when_applicable",
            rolling_ok,
            {"can_tx_raw": raw_rolling, "kvaser": physical_rolling},
        )
        check(
            "m7_single_boot_session",
            len(self.boot_session_ids) == 1,
            {"ids": [f"0x{x:016X}" for x in sorted(self.boot_session_ids)]},
        )
        check(
            "m4_single_boot_id",
            len(self.m4_boot_ids) == 1,
            {"ids": [f"0x{x:08X}" for x in sorted(self.m4_boot_ids)]},
        )
        check(
            "firmware_single_build",
            len(self.build_ids) == 1,
            {"ids": [f"0x{x:08X}" for x in sorted(self.build_ids)]},
        )
        if self.args.expect_env:
            # CAPABILITY reserves 48 bytes and always keeps byte 47 as NUL.
            expected_wire = self.args.expect_env[:47].split("\x00", 1)[0]
            check(
                "firmware_environment",
                self.capability_environments == {expected_wire},
                {"expected": expected_wire,
                 "observed": sorted(self.capability_environments)},
            )

        control_stats = {}
        for can_id in CONTROL_IDS:
            frames = self.control_frames[can_id]
            intervals = [
                (frames[index]["kvaser_timestamp_ms"]
                 - frames[index - 1]["kvaser_timestamp_ms"]) & 0xFFFFFFFF
                for index in range(1, len(frames))
            ]
            stats = {
                "count": len(frames),
                "period_median_ms": statistics.median(intervals) if intervals else None,
                "period_p95_ms": percentile(intervals, 0.95),
                "period_max_ms": max(intervals) if intervals else None,
                "last_data": frames[-1]["data"] if frames else None,
            }
            control_stats[f"0x{can_id:X}"] = stats
            check(
                f"can_0x{can_id:X}_continuity",
                len(frames) >= 2 and max(intervals) <= 100 if intervals else False,
                stats,
            )

        motion = {
            "rpm_min": min(self.motor_rpm) if self.motor_rpm else None,
            "rpm_max": max(self.motor_rpm) if self.motor_rpm else None,
            "steering_deci_degree_min": min(self.steering_deci_degree)
            if self.steering_deci_degree else None,
            "steering_deci_degree_max": max(self.steering_deci_degree)
            if self.steering_deci_degree else None,
        }
        channel_motion = {}
        if self.remote_records:
            ch2 = [item["raw_ch2"] for item in self.remote_records]
            ch4 = [item["raw_ch4"] for item in self.remote_records]
            channel_motion = {
                "ch2_min": min(ch2), "ch2_max": max(ch2),
                "ch2_span": max(ch2) - min(ch2),
                "ch4_min": min(ch4), "ch4_max": max(ch4),
                "ch4_span": max(ch4) - min(ch4),
            }
        if self.args.require_motion:
            rpm_span = max(self.motor_rpm) - min(self.motor_rpm) if self.motor_rpm else 0
            steering_span = (
                max(self.steering_deci_degree) - min(self.steering_deci_degree)
                if self.steering_deci_degree else 0
            )
            check(
                "remote_channel_motion",
                channel_motion.get("ch2_span", 0) >= 300
                and channel_motion.get("ch4_span", 0) >= 300,
                channel_motion,
            )
            check(
                "physical_can_motion",
                rpm_span >= 100 and steering_span >= 50,
                {"rpm_span": rpm_span, "steering_span": steering_span, **motion},
            )

        health_delta = {}
        if self.health_records:
            first = self.health_records[0]
            last = self.health_records[-1]
            for field in (
                "can_drop_total", "fifo_overflow_total", "mcp_tx_fail_total",
                "builtin_tx_fail_total", "mcp_spi_error_total",
                "mcp_error_flags_total", "usb_reconnect_count",
                "usb_forced_reset_count", "usb_offer_overflow_total",
                "wifi_offer_overflow_total", "wifi_socket_error_total",
            ):
                if field in first and field in last:
                    health_delta[field] = counter_delta(first[field], last[field])
            check(
                "board_health_counter_deltas",
                all(value == 0 for value in health_delta.values()),
                health_delta,
            )

        remote_delta = {}
        if self.remote_records:
            first = self.remote_records[0]
            last = self.remote_records[-1]
            for field in (
                "rx_bytes", "accepted_rc_frames", "candidate_updates",
                "candidate_rejects", "malformed_total", "admission_resets",
                "foreground_budget_hits", "ipc_rejects", "shared_publish_failures",
            ):
                remote_delta[field] = counter_delta(first[field], last[field])
            check(
                "remote_runtime_integrity",
                remote_delta["malformed_total"] == 0
                and remote_delta["ipc_rejects"] == 0
                and remote_delta["shared_publish_failures"] == 0,
                remote_delta,
            )

        check(
            "strict_faults",
            not self.fault_counts,
            {"counts": self.fault_counts},
        )
        passed = bool(checks) and all(item["pass"] for item in checks)
        return (
            checks, passed, control_stats, motion, channel_motion,
            health_delta, remote_delta, correlation,
        )

    def finalize(self):
        if self.finalized:
            return None
        self.finalized = True
        now_ns = time.monotonic_ns()
        self._close_serial("monitor finalized", now_ns, strict_disconnect=False)
        try:
            self.kvaser.stop()
        finally:
            self._drain_kvaser()

        (
            checks, passed, control_stats, motion, channel_motion,
            health_delta, remote_delta, correlation,
        ) = self._checks_and_summary()
        summary = {
            "schema": 1,
            "pass": passed,
            "started_utc": self.started_utc,
            "ended_utc": utc_now(),
            "requested_seconds": self.args.seconds,
            "capture_start_monotonic_ns": self.capture_start_ns,
            "completed_requested_window": self.completed_window,
            "interrupted": self.interrupted,
            "exception": self.exception,
            "usb": {
                "epoch_count": self.usb_epoch_count,
                "disconnect_count": self.usb_disconnect_count,
                "raw_artifacts": self.raw_paths,
                "firmware_connection_epochs": sorted(self.usb_health_epochs),
                "cold_enumeration_observed": self.cold_enumeration_observed,
            },
            "typed": {
                "record_counts": {str(key): value for key, value in sorted(self.record_counts.items())},
                "bad_crc": self.typed_bad_crc,
                "sequence_gap_events": self.typed_seq_gap_events,
                "sequence_missing_modulo_total": self.typed_seq_missing_total,
                "resync_discarded_bytes": self.typed_resync_discarded,
            },
            "identity": {
                "m7_boot_session_ids": [
                    f"0x{x:016X}" for x in sorted(self.boot_session_ids)
                ],
                "m7_boot_session_sources": {
                    key: sorted(value) for key, value in self.boot_session_sources.items()
                },
                "m4_boot_ids": [f"0x{x:08X}" for x in sorted(self.m4_boot_ids)],
                "build_ids": [f"0x{x:08X}" for x in sorted(self.build_ids)],
                "capability_environments": sorted(self.capability_environments),
                "board_boot_events": self.board_boot_events,
            },
            "kvaser": {
                "total_frames": self.kvaser_total_frames,
                "scope_frames": self.kvaser_scope_frames,
                "error_frames": self.kvaser_error_frames,
                "read_errors": self.kvaser_read_errors,
                "control": control_stats,
                "motion": motion,
            },
            "runtime_diagnostic": {
                "records": self.diagnostic_records,
                "phase_counts": {
                    DIAGNOSTIC_PHASES.get(key, str(key)): value
                    for key, value in sorted(self.diagnostic_phase_counts.items())
                },
                "attempts_observed": len(self.diagnostic_attempts),
                "fdcan_fault_samples": self.fdcan_fault_samples,
                "fdcan_max_tec": self.fdcan_max_tec,
                "fdcan_max_rec": self.fdcan_max_rec,
                "fdcan_max_write_duration_us": self.fdcan_max_write_duration_us,
                "fdcan_write_failures": self.fdcan_write_failures,
                "fdcan_outcome_failures": self.fdcan_outcome_failures,
                "first": self.diagnostic_first,
                "last": self.diagnostic_last,
            },
            "can_evidence_correlation": correlation,
            "remote_channel_motion": channel_motion,
            "board_health_counter_deltas": health_delta,
            "remote_counter_deltas": remote_delta,
            "faults": {
                "counts": self.fault_counts,
                "examples": self.fault_examples,
            },
            "checks": checks,
        }
        (self.run_dir / "summary.json").write_text(
            json.dumps(summary, indent=2, ensure_ascii=False, sort_keys=True),
            encoding="utf-8",
        )
        self.manifest.update({
            "ended_utc": summary["ended_utc"],
            "verdict": "PASS" if passed else "FAIL",
            "usb_epoch_artifacts": self.raw_paths,
        })
        self._write_manifest()
        self.timeline_handle.close()
        self.kvaser_handle.close()
        self.evidence_handle.close()

        hash_lines = []
        for path in sorted(self.run_dir.iterdir(), key=lambda item: item.name):
            if not path.is_file() or path.name == "SHA256SUMS.txt":
                continue
            digest = hashlib.sha256()
            with path.open("rb") as handle:
                while True:
                    chunk = handle.read(1024 * 1024)
                    if not chunk:
                        break
                    digest.update(chunk)
            hash_lines.append(f"{digest.hexdigest()}  {path.name}")
        (self.run_dir / "SHA256SUMS.txt").write_text(
            "\n".join(hash_lines) + "\n", encoding="ascii"
        )
        return summary


def build_parser():
    parser = argparse.ArgumentParser(
        description=(
            "Monitor CSM boot/runtime diagnostics over USB while a physical "
            "Kvaser channel independently observes CAN."
        )
    )
    parser.add_argument("--seconds", type=float, default=120.0)
    parser.add_argument("--channel", type=int, default=0)
    parser.add_argument("--vid", type=parse_int, default=DEFAULT_VID)
    parser.add_argument("--pid", type=parse_int, default=DEFAULT_PID)
    parser.add_argument("--serial", help="Exact USB serial number; recommended if multiple boards exist")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output-root", default="artifacts/monitor_remote_boot_hil")
    parser.add_argument("--expect-env", help="Exact PlatformIO environment expected in CAPABILITY")
    parser.add_argument(
        "--fdcan-write-budget-us", "--fdcan-budget-us",
        dest="fdcan_write_budget_us", type=int, default=2000,
    )
    parser.add_argument(
        "--correlation-window-ms", type=float, default=250.0,
        help=(
            "Maximum physical-CAN-to-typed host timestamp distance; "
            "matches outside it remain unmatched"
        ),
    )
    parser.add_argument("--require-motion", action="store_true")
    parser.add_argument(
        "--continue-after-fault", action="store_true",
        help="Keep the requested capture window after strict faults instead of stopping after the fault tail.",
    )
    parser.add_argument(
        "--cold-enumeration", action="store_true",
        help=(
            "Require an absent-to-present USB transition. If already present, "
            "wait for disappearance before accepting the next enumeration."
        ),
    )
    parser.add_argument("--enumeration-timeout", type=float, default=60.0)
    return parser


def validate_args(args, parser):
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    if args.channel < 0:
        parser.error("--channel must be non-negative")
    if not 0 <= args.vid <= 0xFFFF or not 0 <= args.pid <= 0xFFFF:
        parser.error("--vid and --pid must be 16-bit values")
    if args.baud <= 0:
        parser.error("--baud must be positive")
    if args.fdcan_write_budget_us <= 0:
        parser.error("--fdcan-write-budget-us must be positive")
    if args.correlation_window_ms <= 0:
        parser.error("--correlation-window-ms must be positive")
    if args.enumeration_timeout <= 0:
        parser.error("--enumeration-timeout must be positive")


def main():
    parser = build_parser()
    args = parser.parse_args()
    validate_args(args, parser)
    monitor = Monitor(args)
    exit_code = 2
    try:
        monitor.run()
    except KeyboardInterrupt:
        monitor.interrupted = True
        monitor.fault("operator_interrupt", {"message": "KeyboardInterrupt"})
    except Exception as exc:
        monitor.exception = {
            "type": type(exc).__name__,
            "message": str(exc),
            "traceback": traceback.format_exc(),
        }
        monitor.fault("monitor_exception", monitor.exception)
    finally:
        summary = monitor.finalize()
        if summary is not None:
            print(f"verdict={'PASS' if summary['pass'] else 'FAIL'}")
            for check in summary["checks"]:
                print(
                    f"{'PASS' if check['pass'] else 'FAIL'} "
                    f"{check['name']}: {check['detail']}"
                )
            print(f"artifacts={monitor.run_dir}")
            exit_code = 0 if summary["pass"] else 2
    raise SystemExit(exit_code)


if __name__ == "__main__":
    main()
