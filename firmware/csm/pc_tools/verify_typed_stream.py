import argparse
import datetime
import struct
import time

import serial


SOF = b"\xA5\x5A"
TYPE_NAMES = {
    1: "CAN_RX_RAW",
    2: "CAN_TX_RAW",
    3: "ENC_EDGE_RAW",
    4: "ENC_DERIVED",
    5: "ADC_SAMPLE",
    6: "CONTROL_ACK",
    7: "BOARD_EVENT",
    8: "BOARD_HEALTH",
    9: "CAPABILITY",
    10: "HOST_CAN_TX_REQUEST",
    11: "HOST_HEARTBEAT",
    12: "HOST_CONTROL_SESSION",
    13: "HOST_SET_CONTROL_POLICY",
    14: "HOST_QUERY_CAPABILITY",
    16: "CAN_RX_SEGMENT",
    17: "STREAM_SESSION",
    18: "REMOTE_CONTROL_STATE",
    19: "RUNTIME_DIAGNOSTIC",
    20: "TRANSPORT_DIAGNOSTIC",
}

WIFI_CALL_PHASES = {
    0: "idle", 1: "configure_ip", 2: "begin_access_point",
    3: "begin_server", 4: "accept_client", 5: "configure_client",
    6: "send", 7: "receive", 8: "close_client", 9: "delete_client",
    10: "accept_extra_client", 11: "close_extra_client",
    12: "delete_extra_client", 13: "stop_server",
    14: "stop_access_point", 15: "begin_control_server",
    16: "accept_control_client", 17: "configure_control_client",
    18: "send_control", 19: "receive_control",
    20: "close_control_client", 21: "stop_control_server",
    22: "open_telemetry_server", 23: "configure_telemetry_server",
    24: "bind_telemetry_server", 25: "listen_telemetry_server",
    26: "open_control_server", 27: "configure_control_server",
    28: "bind_control_server", 29: "listen_control_server",
}

BUS_ROLE_NAMES = {
    0: "role_hint_unknown",
    1: "monitor/system",
    2: "drive/control",
    3: "debug/legacy",
}

BUS_BACKEND_NAMES = {
    1: "MCP2515",
    2: "ArduinoCAN",
    3: "STM32HAL",
    4: "pending",
    5: "RP2040FeederUART",
}

BUS_TRANSCEIVER_NAMES = {
    1: "TJA1050",
    2: "TJA1051",
    3: "MidCarrierU2",
    4: "MCP25625Integrated",
    255: "unknown",
}

EVENT_NAMES = {
    1: "BOOT",
    2: "CAN_BEGIN_FAILED",
    3: "CAN_RX_QUEUE_DROP",
    4: "ENCODER_FAULT_ASSERTED",
    5: "FIELD_POWER_LOST",
    6: "ESTOP_ASSERTED",
    7: "ENCODER_INDEX",
    8: "ENCODER_WRAP",
    9: "MCP2515_ERROR",
    10: "MCP2515_SPI_SNAPSHOT",
    11: "BUILTIN_CAN_BEGIN_FAILED",
    12: "BUILTIN_CAN_TX_FAILED",
    13: "HOST_FRAME_CRC_FAILED",
    14: "HOST_CAN_TX_REJECTED",
    15: "HOST_CAN_TX_ACCEPTED",
    16: "CAN0_BACKEND_UNAVAILABLE",
    17: "MCP2515_TX_FAILED",
    18: "SAFETY_STATE_CHANGED",
    19: "HOST_HEARTBEAT",
    20: "HOST_CONTROL_SESSION",
    21: "HOST_COMMAND_UNSUPPORTED",
    22: "FAULT_LOCKOUT_CLEARED",
    23: "FIRMWARE_IDENTITY",
    24: "SERIAL_TX_BACKPRESSURE_RECOVERY",
    25: "SERIAL_TX_RING_CLEAR",
    26: "CAN_RX_SEGMENT_ENQUEUE_FAILED",
    27: "USB_CDC_SESSION_OPEN",
    28: "USB_CDC_SESSION_CLOSE",
    29: "USB_CDC_DTR_CHANGE",
    30: "USB_HOST_ABSENT_CAN_DISCARD_SUMMARY",
    31: "MCP_PASSIVE_MODE_READBACK",
    32: "MCP_PASSIVE_MODE_VIOLATION",
    33: "MCP_TXREQ_VIOLATION",
    34: "TRANSCEIVER_SAFE_STATE_CHANGED",
    35: "USB_POWER_OR_RESET_SUSPECTED",
    36: "CAN_FRONTEND_PRESESSION_HOLD",
    37: "CAN_FRONTEND_SESSION_READY",
    38: "CAN_FRONTEND_SESSION_INIT_FAILED",
    39: "CAN_FRONTEND_FAULT_HOLD",
    40: "WIFI_TX_BACKPRESSURE",
    41: "RUNTIME_BREADCRUMB_RECOVERED",
    42: "REMOTE_CONTROL_INIT_FAILED",
    43: "REMOTE_CONTROL_STATE_CHANGED",
    44: "WIFI_QUEUE_PRESSURE_ISOLATED",
    45: "WIFI_CLIENT_CLOSED",
    46: "FEEDER_LINK_STARTED",
    47: "FEEDER_SESSION_CHANGED",
    48: "FEEDER_SEQUENCE_GAP",
    49: "FEEDER_TRANSPORT_ERROR",
    50: "FEEDER_SOURCE_FAULT",
    51: "FEEDER_LINK_STALE",
}

CAN_RX_SEGMENT_TYPE = 16
CAN_RX_SEGMENT_SCHEMA_V2 = 2
CAN_RX_SEGMENT_V2_HEADER_LEN = 40
CAN_RX_SEGMENT_V2_ENTRY_LEN = 20
CAN_RX_SEGMENT_LEGACY_HEADER_LEN = 32
CAN_RX_SEGMENT_LEGACY_ENTRY_LEN = 30
CAN_RX_SEGMENT_FLAG_CAPTURE_SEQUENCE_VALID = 1 << 0
CAN_RX_SEGMENT_FLAG_COMPACT_ENTRIES = 1 << 1


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def u16(payload: bytes, offset: int) -> int:
    return struct.unpack_from("<H", payload, offset)[0]


def u32(payload: bytes, offset: int) -> int:
    return struct.unpack_from("<I", payload, offset)[0]


def u64(payload: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", payload, offset)[0]


def i32(payload: bytes, offset: int) -> int:
    return struct.unpack_from("<i", payload, offset)[0]


def i16(payload: bytes, offset: int) -> int:
    return struct.unpack_from("<h", payload, offset)[0]


def i64(payload: bytes, offset: int) -> int:
    return struct.unpack_from("<q", payload, offset)[0]


def decode_can_rx_segment(payload: bytes) -> dict:
    """Decode the canonical CAN_RX_SEGMENT schema 2 or legacy schema 1.

    This is the Python evidence-tool authority for segment reconstruction.
    Callers must treat ValueError as an integrity failure rather than silently
    ignoring a record they cannot interpret.
    """
    if len(payload) < CAN_RX_SEGMENT_LEGACY_HEADER_LEN:
        raise ValueError(f"CAN_RX_SEGMENT header truncated: {len(payload)}")

    segment_sequence = u64(payload, 0)
    first_capture_sequence = u64(payload, 8)
    frame_count = u16(payload, 16)
    entry_size = payload[18]
    flags = payload[19]
    dropped_total = u32(payload, 20)
    fifo_overflow_total = u32(payload, 24)
    schema = payload[28]
    declared_header_size = payload[29]
    compact = bool(flags & CAN_RX_SEGMENT_FLAG_COMPACT_ENTRIES)
    capture_sequence_valid = bool(
        flags & CAN_RX_SEGMENT_FLAG_CAPTURE_SEQUENCE_VALID
    )

    if schema == CAN_RX_SEGMENT_SCHEMA_V2 or compact:
        if schema != CAN_RX_SEGMENT_SCHEMA_V2:
            raise ValueError(
                f"CAN_RX_SEGMENT compact flag with unsupported schema {schema}"
            )
        if declared_header_size != CAN_RX_SEGMENT_V2_HEADER_LEN:
            raise ValueError(
                "CAN_RX_SEGMENT v2 header size "
                f"{declared_header_size}, expected {CAN_RX_SEGMENT_V2_HEADER_LEN}"
            )
        if entry_size != CAN_RX_SEGMENT_V2_ENTRY_LEN:
            raise ValueError(
                "CAN_RX_SEGMENT v2 entry size "
                f"{entry_size}, expected {CAN_RX_SEGMENT_V2_ENTRY_LEN}"
            )
        if not compact or not capture_sequence_valid:
            raise ValueError(
                f"CAN_RX_SEGMENT v2 required flags missing: 0x{flags:02X}"
            )
        if payload[30:32] != b"\x00\x00":
            raise ValueError("CAN_RX_SEGMENT v2 reserved header bytes are non-zero")
        if frame_count > 23:
            raise ValueError(f"CAN_RX_SEGMENT v2 frame count exceeds 23: {frame_count}")
        required = declared_header_size + frame_count * entry_size
        if len(payload) != required:
            raise ValueError(
                f"CAN_RX_SEGMENT v2 length mismatch: {len(payload)} != {required}"
            )
        base_mono_us = u64(payload, 32)
        entries_offset = declared_header_size
        frames = []
        for index in range(frame_count):
            offset = entries_offset + index * entry_size
            capture_delta = u16(payload, offset)
            mono_delta_us = u32(payload, offset + 2)
            dlc_flags = payload[offset + 10]
            if dlc_flags & 0x0F > 8:
                raise ValueError(
                    f"CAN_RX_SEGMENT v2 invalid DLC at entry {index}: {dlc_flags & 0x0F}"
                )
            frames.append(
                {
                    "capture_sequence": (
                        first_capture_sequence + capture_delta
                        if capture_sequence_valid
                        else None
                    ),
                    "mono_us": base_mono_us + mono_delta_us,
                    "can_id_flags": u32(payload, offset + 6),
                    "dlc_flags": dlc_flags,
                    "bus": payload[offset + 11],
                    "data": payload[offset + 12 : offset + 20],
                }
            )
    else:
        if entry_size < CAN_RX_SEGMENT_LEGACY_ENTRY_LEN:
            raise ValueError(
                "CAN_RX_SEGMENT legacy entry size "
                f"{entry_size}, expected at least {CAN_RX_SEGMENT_LEGACY_ENTRY_LEN}"
            )
        if frame_count > 15:
            raise ValueError(
                f"CAN_RX_SEGMENT legacy frame count exceeds 15: {frame_count}"
            )
        required = CAN_RX_SEGMENT_LEGACY_HEADER_LEN + frame_count * entry_size
        if len(payload) != required:
            raise ValueError(
                f"CAN_RX_SEGMENT legacy length mismatch: {len(payload)} != {required}"
            )
        frames = []
        for index in range(frame_count):
            offset = CAN_RX_SEGMENT_LEGACY_HEADER_LEN + index * entry_size
            dlc_flags = payload[offset + 20]
            if dlc_flags & 0x0F > 8:
                raise ValueError(
                    "CAN_RX_SEGMENT legacy invalid DLC at entry "
                    f"{index}: {dlc_flags & 0x0F}"
                )
            frames.append(
                {
                    "capture_sequence": u64(payload, offset),
                    "mono_us": u64(payload, offset + 8),
                    "can_id_flags": u32(payload, offset + 16),
                    "dlc_flags": dlc_flags,
                    "bus": payload[offset + 21],
                    "data": payload[offset + 22 : offset + 30],
                }
            )

    return {
        "schema": schema,
        "segment_sequence": segment_sequence,
        "first_capture_sequence": first_capture_sequence,
        "frame_count": frame_count,
        "entry_size": entry_size,
        "flags": flags,
        "dropped_total": dropped_total,
        "fifo_overflow_total": fifo_overflow_total,
        "frames": frames,
    }


def zstr(payload: bytes, offset: int, size: int) -> str:
    raw = payload[offset : offset + size]
    raw = raw.split(b"\x00", 1)[0]
    return raw.decode("ascii", errors="replace")


def format_epoch(epoch: int) -> str:
    if epoch == 0:
        return "unknown"
    return datetime.datetime.fromtimestamp(epoch).isoformat(timespec="seconds")


def capability_bus_desc(payload: bytes, offset: int) -> str:
    bus_id = payload[offset]
    role = payload[offset + 1]
    backend = payload[offset + 2]
    transceiver = payload[offset + 3]
    rx_supported = payload[offset + 4]
    tx_supported = payload[offset + 5]
    control_tx_allowed = payload[offset + 6]
    classic = payload[offset + 7]
    can_fd = payload[offset + 8]
    max_dlc = payload[offset + 9]
    nominal = u32(payload, offset + 10)
    termination = payload[offset + 18]
    isolation = payload[offset + 19]
    return (
        f"bus{bus_id}:role={BUS_ROLE_NAMES.get(role, role)} "
        f"backend={BUS_BACKEND_NAMES.get(backend, backend)} "
        f"xcvr={BUS_TRANSCEIVER_NAMES.get(transceiver, transceiver)} "
        f"rx={rx_supported} tx={tx_supported} control={control_tx_allowed} "
        f"classic={classic} fd={can_fd} max_dlc={max_dlc} bitrate={nominal} "
        f"term={termination} iso={isolation}"
    )


def parse_frame(buf: bytearray):
    start = buf.find(SOF)
    if start < 0:
        del buf[:-1]
        return None
    if start > 0:
        del buf[:start]
    if len(buf) < 11:
        return None

    version = buf[2]
    rtype = buf[3]
    flags = buf[4]
    seq = u16(buf, 5)
    length = u16(buf, 7)
    frame_len = 2 + 1 + 1 + 1 + 2 + 2 + length + 2
    if length > 512:
        del buf[:2]
        return None
    if len(buf) < frame_len:
        return None

    frame = bytes(buf[:frame_len])
    del buf[:frame_len]
    expected = u16(frame, frame_len - 2)
    actual = crc16_ccitt(frame[2:-2])
    if expected != actual:
        return {"bad_crc": True, "expected": expected, "actual": actual}
    return {
        "version": version,
        "type": rtype,
        "flags": flags,
        "seq": seq,
        "payload": frame[9:-2],
    }


def decode_transport_diagnostic(payload: bytes):
    if len(payload) < 192 or payload[8] != 4:
        return None
    call_flags = payload[141]
    return {
        "mono_us": u64(payload, 0),
        "schema": payload[8],
        "flags": payload[9],
        "close_reason": payload[10],
        "runtime_mode": payload[11],
        "connection_epoch": u32(payload, 12),
        "offered_bytes": u32(payload, 16),
        "accepted_bytes": u32(payload, 20),
        "accepted_records": u32(payload, 24),
        "rejected_records": u32(payload, 28),
        "pending_queue_bytes": u32(payload, 32),
        "pending_queue_records": u32(payload, 36),
        "positive_socket_bytes": u32(payload, 48),
        "completed_socket_records": u32(payload, 52),
        "socket_errors": u32(payload, 64),
        "last_network_error": i32(payload, 128),
        "last_failure_phase": payload[132],
        "last_failure_phase_name": WIFI_CALL_PHASES.get(
            payload[132], f"unknown_{payload[132]}"
        ),
        "last_failure_result": i32(payload, 136),
        "current_call_phase": payload[140],
        "current_call_phase_name": WIFI_CALL_PHASES.get(
            payload[140], f"unknown_{payload[140]}"
        ),
        "current_call_coherent": bool(call_flags & 1),
        "current_call_in_progress": bool(call_flags & 2),
        "current_call_sequence": u32(payload, 144),
        "current_call_started_ms": u32(payload, 148),
        "current_call_duration_us": u32(payload, 152),
        "current_call_result": i32(payload, 156),
        "worker_heartbeat_age_ms": u32(payload, 160),
        "control_connection_epoch": u32(payload, 164),
        "control_connected": bool(payload[168] & 1),
        "configured_socket_max": payload[169],
        "configured_tcp_socket_max": payload[170],
        "configured_tcp_server_max": payload[171],
        "required_application_sockets": payload[172],
        "required_total_socket_arena": payload[173],
        "socket_arena_capacity": u32(payload, 176),
        "socket_arena_used": u32(payload, 180),
        "socket_arena_high_water": u32(payload, 184),
        "socket_arena_allocation_failures": u32(payload, 188),
    }


def describe(frame):
    if frame.get("bad_crc"):
        return f"[BAD_CRC] expected=0x{frame['expected']:04X} actual=0x{frame['actual']:04X}"

    rtype = frame["type"]
    payload = frame["payload"]
    name = TYPE_NAMES.get(rtype, f"type_{rtype}")
    seq = frame["seq"]

    if rtype in (1, 2) and len(payload) >= 30:
        mono = u64(payload, 0)
        can_id_flags = u32(payload, 8)
        can_id = can_id_flags & 0x1FFFFFFF
        ext = (can_id_flags >> 29) & 1
        dlc = payload[12] & 0x0F
        bus = payload[13]
        data = payload[14:22]
        total = u32(payload, 22)
        fail_or_drop = u32(payload, 26)
        tail = (
            f"rx_total={total} dropped={fail_or_drop}"
            if rtype == 1
            else f"tx_total={total} failed={fail_or_drop}"
        )
        return (
            f"[{name}] seq={seq} mono_us={mono} bus={bus} id=0x{can_id:X} ext={ext} "
            f"dlc={dlc} data={data.hex(' ')} {tail}"
        )

    if rtype == CAN_RX_SEGMENT_TYPE:
        try:
            segment = decode_can_rx_segment(payload)
        except ValueError as exc:
            return f"[{name}] seq={seq} INVALID {exc}"
        preview = []
        for index, entry in enumerate(segment["frames"][:3]):
            can_id = entry["can_id_flags"] & 0x1FFFFFFF
            dlc = entry["dlc_flags"] & 0x0F
            preview.append(
                f"#{index} cap={entry['capture_sequence']} t={entry['mono_us']} "
                f"bus={entry['bus']} id=0x{can_id:X} dlc={dlc} "
                f"data={entry['data'].hex(' ')}"
            )
        return (
            f"[{name}] seq={seq} segment_seq={segment['segment_sequence']} "
            f"first_capture_seq={segment['first_capture_sequence']} "
            f"frames={segment['frame_count']} entry={segment['entry_size']} "
            f"schema={segment['schema']} dropped_before={segment['dropped_total']} "
            f"fifo_before={segment['fifo_overflow_total']} "
            + " | ".join(preview)
        )

    if rtype == 3 and len(payload) >= 28:
        return (
            f"[{name}] seq={seq} mono_us={u64(payload, 0)} pos={i64(payload, 8)} "
            f"tim3={u16(payload, 16)} ab={payload[18]} flags=0x{payload[19]:02X} "
            f"fault=0x{u32(payload, 20):08X} z_count={u32(payload, 24)}"
        )

    if rtype == 4 and len(payload) >= 28:
        return (
            f"[{name}] seq={seq} mono_us={u64(payload, 0)} pos={i64(payload, 8)} "
            f"cps={i32(payload, 16)} tim3={u16(payload, 20)} ab={payload[22]} "
            f"fault=0x{u32(payload, 24):08X}"
        )

    if rtype == 5 and len(payload) >= 44:
        source = payload[16]
        count = min(payload[17], 8)
        bits = payload[18]
        flags = payload[19]
        channels = payload[20 : 20 + count]
        samples = [u16(payload, 28 + i * 2) for i in range(count)]
        pairs = " ".join(f"ch{channels[i]}={samples[i]}" for i in range(count))
        return (
            f"[{name}] seq={seq} mono_us={u64(payload, 0)} sample_total={u32(payload, 8)} "
            f"dropped={u32(payload, 12)} source={source} bits={bits} "
            f"flags=0x{flags:02X} {pairs}"
        )

    if rtype == 6 and len(payload) >= 28:
        status = payload[12]
        reason = payload[13]
        bus = payload[14]
        dlc = payload[15] & 0x0F
        can_id_flags = u32(payload, 16)
        can_id = can_id_flags & 0x1FFFFFFF
        ext = (can_id_flags >> 29) & 1
        rtr = (can_id_flags >> 30) & 1
        return (
            f"[{name}] seq={seq} mono_us={u64(payload, 0)} command=0x{u32(payload, 8):08X} "
            f"status={status} reason={reason} bus={bus} id=0x{can_id:X} ext={ext} rtr={rtr} "
            f"dlc={dlc} counter={u32(payload, 20)} rejected={u32(payload, 24)}"
        )

    if rtype == 7 and len(payload) >= 16:
        code = u16(payload, 8)
        event_name = EVENT_NAMES.get(code, f"code_{code}")
        if code == 23:
            detail = u16(payload, 10)
            identity_version = detail & 0xFF
            dirty = (detail >> 8) & 0xFF
            return (
                f"[{name}] seq={seq} mono_us={u64(payload, 0)} code={code} "
                f"name={event_name} identity_v={identity_version} dirty={dirty} "
                f"build_id=0x{u32(payload, 12):08X}"
            )
        if code == 10:
            detail = u16(payload, 10)
            counter = u32(payload, 12)
            stage = (detail >> 8) & 0xFF
            canstat = detail & 0xFF
            canctrl = (counter >> 24) & 0xFF
            canintf = (counter >> 16) & 0xFF
            eflg = (counter >> 8) & 0xFF
            extra = counter & 0xFF
            return (
                f"[{name}] seq={seq} mono_us={u64(payload, 0)} code={code} "
                f"stage={stage} CANSTAT=0x{canstat:02X} CANCTRL=0x{canctrl:02X} "
                f"CANINTF=0x{canintf:02X} EFLG=0x{eflg:02X} extra=0x{extra:02X}"
            )
        if code == 9:
            detail = u16(payload, 10)
            counter = u32(payload, 12)
            stage = (detail >> 8) & 0xFF
            if stage == 0:
                return (
                    f"[{name}] seq={seq} mono_us={u64(payload, 0)} code={code} "
                    f"name={event_name} operation={counter} mcp_error={detail}"
                )
            canintf = detail & 0xFF
            eflg = (counter >> 24) & 0xFF
            canctrl = (counter >> 16) & 0xFF
            spi_err_low = counter & 0xFFFF
            return (
                f"[{name}] seq={seq} mono_us={u64(payload, 0)} code={code} "
                f"name={event_name} stage={stage} CANINTF=0x{canintf:02X} "
                f"EFLG=0x{eflg:02X} CANCTRL=0x{canctrl:02X} spi_err_low={spi_err_low}"
            )
        return (
            f"[{name}] seq={seq} mono_us={u64(payload, 0)} code={code} name={event_name} "
            f"detail={u16(payload, 10)} counter={u32(payload, 12)}"
        )

    if rtype == 8 and len(payload) >= 52:
        extra = ""
        if len(payload) >= 128 and payload[52] >= 2:
            extra = (
                f" health_v={payload[52]} safety_v2={payload[54]} fault_bits=0x{payload[55]:02X}"
                f" heartbeat_age_ms={u32(payload, 56)} lease_ms={u32(payload, 60)}"
                f" host_crc={u32(payload, 64)} host_req={u32(payload, 68)}"
                f" host_acc={u32(payload, 72)} host_rej={u32(payload, 76)}"
                f" mcp_tx={u32(payload, 80)} mcp_fail={u32(payload, 84)}"
                f" builtin_tx={u32(payload, 88)} builtin_fail={u32(payload, 92)}"
                f" mcp_spi_err={u32(payload, 96)} mcp_err_flags={u32(payload, 100)}"
                f" mcp_canintf=0x{payload[104]:02X} mcp_eflg=0x{payload[105]:02X}"
                f" mcp_canctrl=0x{payload[106]:02X} mcp_int_low={payload[107]}"
                f" heartbeat_total={u32(payload, 120)} session_total={u32(payload, 124)}"
            )
        if len(payload) >= 192 and payload[52] >= 4:
            extra += (
                f" bus0_rx={u32(payload, 128)} bus0_drop={u32(payload, 132)}"
                f" bus0_q={u32(payload, 136)} bus0_high={u32(payload, 140)}"
                f" bus1_rx={u32(payload, 144)} bus1_drop={u32(payload, 148)}"
                f" bus1_q={u32(payload, 152)} bus1_high={u32(payload, 156)}"
                f" serial_enqueue_fail={u32(payload, 160)}"
                f" serial_clear={u32(payload, 164)}"
                f" serial_clear_bytes={u32(payload, 168)}"
                f" serial_backpressure={u32(payload, 172)}"
                f" serial_high_bytes={u32(payload, 176)}"
                f" can_q_high={u32(payload, 180)}"
                f" mcp_drain_budget_hit={u32(payload, 184)}"
                f" segment_enqueue_fail={u32(payload, 188)}"
            )
        if len(payload) >= 224 and payload[52] >= 5:
            extra += (
                f" uplink_large_used={u32(payload, 192)}"
                f" uplink_large_cap={u32(payload, 196)}"
                f" uplink_can_reserve_used={u32(payload, 200)}"
                f" can_truth_q_high={u32(payload, 204)}"
                f" pool_alloc_fail={u32(payload, 208)}"
                f" can_truth_pool_fail={u32(payload, 212)}"
                f" descriptor_high={u32(payload, 216)}"
                f" diag_suppressed={u32(payload, 220)}"
            )
        if len(payload) >= 260 and payload[52] >= 6:
            extra += (
                f" fw_profile={u32(payload, 224)}"
                f" vehicle_impact={u32(payload, 228)}"
                f" can_rx_task_max_us={u32(payload, 232)}"
                f" uplink_pool_high_bytes={u32(payload, 236)}"
                f" uplink_desc_high={u32(payload, 240)}"
                f" usb_reconnect={u32(payload, 244)}"
                f" usb_forced_reset={u32(payload, 248)}"
                f" passive_violation=0x{u32(payload, 252):08X}"
                f" capture_invalid_reason=0x{u32(payload, 256):08X}"
            )
        if len(payload) >= 296 and payload[52] >= 7:
            extra += (
                f" host_absent_bus0_discard={u32(payload, 260)}"
                f" host_absent_bus1_discard={u32(payload, 264)}"
                f" host_absent_fifo_overflow={u32(payload, 268)}"
                f" host_absent_mcp_error={u32(payload, 272)}"
                f" host_absent_duration_ms={u32(payload, 276)}"
                f" passive_readback={u32(payload, 280)}"
                f" passive_readback_violation={u32(payload, 284)}"
                f" txreq_violation={u32(payload, 288)}"
                f" usb_dtr_change={u32(payload, 292)}"
            )
        if len(payload) >= 360 and payload[52] >= 8:
            extra += (
                f" publish_next={u64(payload, 296)}"
                f" boot_session=0x{u64(payload, 304):016X}"
                f" usb_epoch={u32(payload, 312)}"
                f" usb_high={u32(payload, 316)}"
                f" usb_overflow={u32(payload, 320)}"
                f" usb_sent={u32(payload, 324)}"
                f" wifi_epoch={u32(payload, 328)}"
                f" wifi_high={u32(payload, 332)}"
                f" wifi_overflow={u32(payload, 336)}"
                f" wifi_sent={u32(payload, 340)}"
                f" wifi_connect={u32(payload, 344)}"
                f" wifi_disconnect={u32(payload, 348)}"
                f" wifi_stall_close={u32(payload, 352)}"
                f" no_sink_drop={u32(payload, 356)}"
            )
        if len(payload) >= 384 and payload[52] >= 9:
            extra += (
                f" wifi_socket_error={u32(payload, 360)}"
                f" wifi_send_budget_overrun={u32(payload, 364)}"
                f" wifi_send_call_max_us={u32(payload, 368)}"
                f" wifi_recv_call_max_us={u32(payload, 372)}"
                f" wifi_close_call_max_us={u32(payload, 376)}"
                f" main_loop_max_gap_us={u32(payload, 380)}"
            )
        if len(payload) >= 392 and payload[52] >= 10:
            extra += (
                f" reset_cause=0x{u32(payload, 384):08X}"
                f" reset_status_raw=0x{u32(payload, 388):08X}"
            )
        if len(payload) >= 408 and payload[52] >= 11:
            breadcrumb = u32(payload, 396)
            extra += (
                f" previous_breadcrumb_valid={u32(payload, 392)}"
                f" previous_breadcrumb_stage={breadcrumb & 0xFF}"
                f" previous_breadcrumb_detail={(breadcrumb >> 8) & 0xFF}"
                f" previous_breadcrumb_uptime_ms={u32(payload, 400)}"
                f" previous_breadcrumb_seq={u32(payload, 404)}"
            )
        if len(payload) >= 472 and payload[52] >= 12:
            recovery_flags = u32(payload, 408)
            profile_word = u32(payload, 464)
            integrity_word = u32(payload, 468)
            experiment_flags = (profile_word >> 24) & 0xFF
            extra += (
                f" recovery_flags=0x{recovery_flags:08X}"
                f" recovery_ready={(recovery_flags >> 0) & 1}"
                f" previous_valid={(recovery_flags >> 1) & 1}"
                f" previous_stable={(recovery_flags >> 2) & 1}"
                f" current_stable={(recovery_flags >> 3) & 1}"
                f" wifi_quarantined={(recovery_flags >> 4) & 1}"
                f" wifi_start_allowed={(recovery_flags >> 5) & 1}"
                f" source_id32=0x{u32(payload, 412):08X}"
                f" boot_sequence={u32(payload, 416)}"
                f" consecutive_early_resets={u32(payload, 420)}"
                f" early_reset_total={u32(payload, 424)}"
                f" wifi_quarantine_total={u32(payload, 428)}"
                f" previous_boot_sequence={u32(payload, 432)}"
                f" previous_progress_id={u32(payload, 436)}"
                f" previous_progress_detail={u32(payload, 440)}"
                f" previous_progress_uptime_ms={u32(payload, 444)}"
                f" current_progress_id={u32(payload, 448)}"
                f" current_progress_detail={u32(payload, 452)}"
                f" current_progress_uptime_ms={u32(payload, 456)}"
                f" retained_event_sequence={u32(payload, 460)}"
                f" experiment_selector={profile_word & 0xFF}"
                f" requested_wifi={(profile_word >> 8) & 0xFF}"
                f" effective_wifi={(profile_word >> 16) & 0xFF}"
                f" experiment_flags=0x{experiment_flags:02X}"
                f" watchdog_requested={(experiment_flags >> 3) & 1}"
                f" watchdog_effective={experiment_flags & 1}"
                f" watchdog_start_called={(experiment_flags >> 4) & 1}"
                f" watchdog_start_succeeded={(experiment_flags >> 5) & 1}"
                f" watchdog_timeout_matches={(experiment_flags >> 6) & 1}"
                f" retained_valid_events={integrity_word & 0xFFFF}"
                f" retained_corrupt_metadata={(integrity_word >> 16) & 0xFF}"
                f" retained_corrupt_events={(integrity_word >> 24) & 0xFF}"
            )
        if len(payload) >= 508 and payload[52] >= 13:
            call_flags = u32(payload, 484)
            extra += (
                f" runtime_contract_id32=0x{u32(payload, 472):08X}"
                f" recovery_identity_id32=0x{u32(payload, 476):08X}"
                f" watchdog_timeout_ms={u32(payload, 480)}"
                f" previous_wifi_call_flags=0x{call_flags:08X}"
                f" previous_wifi_call_valid={call_flags & 1}"
                f" previous_wifi_call_in_progress={(call_flags >> 1) & 1}"
                f" previous_wifi_call_completed={(call_flags >> 2) & 1}"
                f" previous_wifi_call_contract_changed={(call_flags >> 3) & 1}"
                f" previous_wifi_call_owner={(call_flags >> 8) & 0xFF}"
                f" previous_wifi_call_operation={(call_flags >> 16) & 0xFF}"
                f" previous_wifi_call_boot={u32(payload, 488)}"
                f" previous_wifi_call_seq={u32(payload, 492)}"
                f" previous_wifi_call_started_ms={u32(payload, 496)}"
                f" previous_wifi_call_duration_us={u32(payload, 500)}"
                f" previous_wifi_call_result={i32(payload, 504)}"
            )
        return (
            f"[{name}] seq={seq} mono_us={u64(payload, 0)} can_rx={u32(payload, 8)} "
            f"can_drop={u32(payload, 12)} fifo_overflow={u32(payload, 16)} "
            f"tx_records={u32(payload, 20)} "
            f"queue={u32(payload, 24)} enc_faults={u32(payload, 28)} "
            f"enc_wrap={u32(payload, 32)} pos={i64(payload, 36)} "
            f"safety={payload[44]} inputs=0x{payload[45]:02X} "
            f"timer_ok={payload[46]} flags=0x{payload[47]:02X} "
            f"can_ok={(payload[47] >> 1) & 1} builtin_can_tx_ok={(payload[47] >> 2) & 1} "
            f"mcp_int_level={(payload[47] >> 3) & 1} mcp_exti_hint={(payload[47] >> 4) & 1} "
            f"voltage_adc_ok={(payload[47] >> 5) & 1} "
            f"fault=0x{u32(payload, 48):08X}{extra}"
        )

    if rtype == 9 and len(payload) >= 36:
        base = (
            f"[{name}] seq={seq} mono_us={u64(payload, 0)} proto={payload[8]} "
            f"profile={payload[9]}.{payload[10]} mono_unit={payload[11]} "
            f"can_q={u32(payload, 12)} encoder_ppr={u32(payload, 16)} "
            f"encoder_freq_limit={u32(payload, 20)} adc_sample={payload[28]} "
            f"adc_channels={payload[31]} adc_bits={payload[32]} adc_period_ms={payload[33]}"
        )
        if len(payload) >= 40:
            bus_count = payload[36]
            desc_size = payload[37]
            flags = u16(payload, 38)
            descs = []
            if desc_size >= 20:
                for index in range(min(bus_count, 2)):
                    offset = 40 + index * desc_size
                    if len(payload) >= offset + 20:
                        descs.append(capability_bus_desc(payload, offset))
            if descs:
                tail = f"{base} cap_v2_flags=0x{flags:04X} " + " | ".join(descs)
                if len(payload) >= 112:
                    tail += (
                        f" uplink_mask=0x{u32(payload, 80):08X}"
                        f" downlink_mask=0x{u32(payload, 84):08X}"
                        f" safety_features=0x{u32(payload, 88):08X}"
                        f" build_id=0x{u32(payload, 96):08X}"
                        f" host_tx_q={u16(payload, 100)}"
                    )
                if len(payload) >= 192 and payload[112] != 0:
                    tail += (
                        f" fw_identity_v={payload[112]}"
                        f" dirty={payload[113]}"
                        f" irq_mode={payload[114]}"
                        f" built={format_epoch(u32(payload, 116))}"
                        f" fw_build_id=0x{u32(payload, 120):08X}"
                        f" mcp_spi_hz={u32(payload, 124)}"
                        f" drain_budget={u16(payload, 128)}"
                        f" serial_ring_kib={u16(payload, 130)}"
                        f" git={zstr(payload, 132, 12)}"
                        f" env={zstr(payload, 144, 48)}"
                    )
                if len(payload) >= 224:
                    tail += (
                        f" fw_profile={payload[192]}"
                        f" profile_lock={payload[193]}"
                        f" vehicle_impact={payload[194]}"
                        f" host_cmd_rx={payload[195]}"
                        f" control_path={payload[196]}"
                        f" usb_isolated={payload[197]}"
                        f" dtr_reset_sensitive={payload[198]}"
                        f" passive_acceptance={payload[199]}"
                        f" hw_case=0x{u32(payload, 200):08X}"
                        f" bench_id=0x{u32(payload, 204):08X}"
                        f" bus0_mode={payload[208]}"
                        f" bus0_ack={payload[209]}"
                        f" bus0_err_frame={payload[210]}"
                        f" bus0_reset_safe={payload[211]}"
                        f" bus1_mode={payload[212]}"
                        f" bus1_ack={payload[213]}"
                        f" bus1_err_frame={payload[214]}"
                        f" bus1_reset_safe={payload[215]}"
                        f" dtr_session_required={payload[216]}"
                        f" dtr_session_only={payload[217]}"
                    )
                return tail
        return base

    if rtype == 20:
        diagnostic = decode_transport_diagnostic(payload)
        if diagnostic is None:
            return f"[{name}] seq={seq} invalid_schema_or_length={len(payload)}"
        return (
            f"[{name}] seq={seq} schema={diagnostic['schema']} "
            f"epoch={diagnostic['connection_epoch']} flags=0x{diagnostic['flags']:02X} "
            f"accepted={diagnostic['accepted_bytes']}/{diagnostic['accepted_records']} "
            f"socket={diagnostic['positive_socket_bytes']}/"
            f"{diagnostic['completed_socket_records']} errors={diagnostic['socket_errors']} "
            f"last_network_error={diagnostic['last_network_error']} "
            f"last_failure={diagnostic['last_failure_phase_name']}:"
            f"{diagnostic['last_failure_result']} "
            f"current_call={diagnostic['current_call_phase_name']}:"
            f"{diagnostic['current_call_result']} "
            f"control_connected={diagnostic['control_connected']} "
            f"socket_budget={diagnostic['configured_socket_max']}/"
            f"{diagnostic['required_total_socket_arena']} "
            f"arena={diagnostic['socket_arena_used']}/"
            f"{diagnostic['socket_arena_capacity']} "
            f"arena_high={diagnostic['socket_arena_high_water']} "
            f"arena_fail={diagnostic['socket_arena_allocation_failures']} "
            f"in_progress={diagnostic['current_call_in_progress']} "
            f"heartbeat_age_ms={diagnostic['worker_heartbeat_age_ms']}"
        )

    if rtype == 18 and len(payload) >= 208:
        flags = payload[12]
        channels = [i16(payload, 128 + index * 2) for index in range(16)]
        raw_channels = [u16(payload, 176 + index * 2) for index in range(16)]
        frontend_tail = ""
        if len(payload) >= 228:
            frontend_tail = (
                f" accepted_rc={u32(payload, 208)}"
                f" normalize_reject={u32(payload, 212)}"
                f" rc_age_ms={u32(payload, 216)}"
                f" link_age_ms={u32(payload, 220)}"
                f" normalize_detail={u16(payload, 224)}"
            )
        return (
            f"[{name}] seq={seq} mono_us={u64(payload, 0)} schema={payload[8]} "
            f"link={payload[9]} authority={payload[10]} source={payload[11]} "
            f"flags=0x{flags:02X} frontend={(flags >> 1) & 1} reserved={(flags >> 2) & 1} "
            f"valid={(flags >> 3) & 1} neutral={(flags >> 4) & 1} "
            f"qualified={(flags >> 5) & 1} released={(flags >> 6) & 1} "
            f"lq={payload[13]} rssi=-{payload[14]}dBm crsf_type=0x{payload[15]:02X} "
            f"m4_boot=0x{u32(payload, 16):08X} shared_seq={u32(payload, 20)} "
            f"age_ms={u32(payload, 24)} drive={i16(payload, 28)} steer={i16(payload, 30)} "
            f"raw_ch2={u16(payload, 32)} raw_ch4={u16(payload, 34)} baud={u32(payload, 36)} "
            f"rx_bytes={u32(payload, 40)} valid_frames={u32(payload, 44)} "
            f"rc_frames={u32(payload, 48)} link_frames={u32(payload, 52)} "
            f"bad_len={u32(payload, 56)} bad_crc={u32(payload, 60)} "
            f"gap_reset={u32(payload, 64)} publish={u32(payload, 68)} "
            f"telem_frames={u32(payload, 72)} telem_bytes={u32(payload, 76)} "
            f"telem_fail={u32(payload, 80)} control_cycles={u32(payload, 84)} "
            f"neutral_cycles={u32(payload, 88)} deadline_miss={u32(payload, 92)} "
            f"can_tx={u32(payload, 96)} can_fail={u32(payload, 100)} "
            f"ipc_reject={u32(payload, 104)} ipc_detail={payload[111]} "
            f"decision={payload[108]} "
            f"sample_state={payload[110]} cycle_ms={u16(payload, 112)} "
            f"frame_gap_ms={u16(payload, 114)} ch={channels} "
            f"link_stats_valid={payload[160]} rssi1=-{payload[161]}dBm "
            f"rssi2=-{payload[162]}dBm snr={struct.unpack_from('<b', payload, 163)[0]}dB "
            f"antenna={payload[164]} rf_profile={payload[165]} rf_power={payload[166]} "
            f"down_rssi=-{payload[167]}dBm down_lq={payload[168]} "
            f"down_snr={struct.unpack_from('<b', payload, 169)[0]}dB "
            f"shared_publish_fail={u32(payload, 170)} "
            f"raw_ch={raw_channels}{frontend_tail}"
        )

    if rtype == 19 and len(payload) >= 128:
        before = [u32(payload, 40 + index * 4) for index in range(8)]
        wifi_flags = payload[73]
        if payload[9] == 7:
            return (
                f"[{name}] seq={seq} recovery_event type={payload[10]} "
                f"event_seq={u32(payload, 12)} boot_seq={u32(payload, 16)} "
                f"uptime_ms={u32(payload, 20)} value=0x{u32(payload, 24):08X} "
                f"code={u32(payload, 28)} identity=0x{u32(payload, 32):08X} "
                f"boot=0x{u64(payload, 104):016X}"
            )
        if payload[9] == 8:
            return (
                f"[{name}] seq={seq} recovered_wifi_call owner={payload[10]} "
                f"boot_seq={u32(payload, 16)} completed_ms={u32(payload, 20)} "
                f"contract=0x{u64(payload, 32):016X} phase={payload[72]} "
                f"call_flags=0x{wifi_flags:02X} call_seq={u32(payload, 76)} "
                f"started_ms={u32(payload, 80)} duration_us={u32(payload, 84)} "
                f"result={i32(payload, 88)} boot=0x{u64(payload, 104):016X}"
            )
        return (
            f"[{name}] seq={seq} mono_us={u64(payload, 0)} schema={payload[8]} "
            f"phase={payload[9]} boot_phase={payload[10]} flags=0x{payload[11]:02X} "
            f"attempt={u32(payload, 12)} can=0x{u32(payload, 16):08X} "
            f"write_us={u32(payload, 20)} rc={i32(payload, 24)} "
            f"tx_request_mask=0x{u32(payload, 28):08X} "
            f"hal_state=0x{u32(payload, 32):08X} "
            f"hal_error=0x{u32(payload, 36):08X} fdcan={before} "
            f"wifi_phase={payload[72]} wifi_flags=0x{wifi_flags:02X} "
            f"wifi_call_seq={u32(payload, 76)} wifi_started_ms={u32(payload, 80)} "
            f"wifi_duration_us={u32(payload, 84)} wifi_result={i32(payload, 88)} "
            f"wifi_heartbeat_age_ms={u32(payload, 92)} "
            f"wifi_stall_total={u32(payload, 96)} wifi_epoch={u32(payload, 100)} "
            f"boot=0x{u64(payload, 104):016X} runtime=0x{u32(payload, 112):08X} "
            f"wifi_stack_free={u32(payload, 116)} "
            f"wifi_stack_max_used={u32(payload, 120)} "
            f"build=0x{u32(payload, 124):08X}"
        )

    return f"[{name}] seq={seq} len={len(payload)} payload={payload.hex(' ')}"


class GapTracker:
    def __init__(self):
        self.last_typed_seq = None
        self.typed_seq_gaps = 0
        self.last_segment_seq = None
        self.segment_seq_gaps = 0
        self.last_capture_seq = None
        self.capture_seq_gaps = 0
        self.segment_decode_errors = 0
        self.last_health = {}

    @staticmethod
    def _seq16_gap(prev: int, cur: int) -> int:
        expected = (prev + 1) & 0xFFFF
        if cur == expected:
            return 0
        return (cur - expected) & 0xFFFF

    @staticmethod
    def _seq64_discontinuity(prev: int, cur: int) -> int:
        if cur == prev + 1:
            return 0
        return cur - prev - 1 if cur > prev + 1 else 1

    def observe(self, frame):
        if frame.get("bad_crc"):
            return

        seq = frame["seq"]
        if self.last_typed_seq is not None:
            self.typed_seq_gaps += self._seq16_gap(self.last_typed_seq, seq)
        self.last_typed_seq = seq

        rtype = frame["type"]
        payload = frame["payload"]
        if rtype == CAN_RX_SEGMENT_TYPE:
            try:
                segment = decode_can_rx_segment(payload)
            except ValueError:
                self.segment_decode_errors += 1
                return
            segment_seq = segment["segment_sequence"]
            if self.last_segment_seq is not None and segment_seq != self.last_segment_seq + 1:
                self.segment_seq_gaps += self._seq64_discontinuity(
                    self.last_segment_seq, segment_seq
                )
            self.last_segment_seq = segment_seq
            for entry in segment["frames"]:
                capture_seq = entry["capture_sequence"]
                if capture_seq is None:
                    continue
                if self.last_capture_seq is not None and capture_seq != self.last_capture_seq + 1:
                    self.capture_seq_gaps += self._seq64_discontinuity(
                        self.last_capture_seq, capture_seq
                    )
                self.last_capture_seq = capture_seq

        if rtype == 8 and len(payload) >= 192 and payload[52] >= 4:
            self.last_health = {
                "serial_enqueue_fail": u32(payload, 160),
                "serial_clear": u32(payload, 164),
                "serial_clear_bytes": u32(payload, 168),
                "serial_backpressure": u32(payload, 172),
                "serial_high_bytes": u32(payload, 176),
                "can_q_high": u32(payload, 180),
                "mcp_drain_budget_hit": u32(payload, 184),
                "segment_enqueue_fail": u32(payload, 188),
                "can_rx_dropped_total": u32(payload, 12),
                "can_fifo_overflow_total": u32(payload, 16),
                "mcp_spi_error": u32(payload, 96),
                "mcp_error_flag": u32(payload, 100),
                "mcp_last_canintf": payload[104],
                "mcp_last_eflg": payload[105],
                "mcp_last_canctrl": payload[106],
                "mcp_last_int_low": payload[107],
            }
            if len(payload) >= 224 and payload[52] >= 5:
                self.last_health.update({
                    "uplink_large_used": u32(payload, 192),
                    "uplink_can_reserve_used": u32(payload, 200),
                    "can_truth_q_high": u32(payload, 204),
                    "pool_alloc_fail": u32(payload, 208),
                    "can_truth_pool_fail": u32(payload, 212),
                    "descriptor_high": u32(payload, 216),
                    "diag_suppressed": u32(payload, 220),
                })
            if len(payload) >= 260 and payload[52] >= 6:
                self.last_health.update({
                    "fw_profile": u32(payload, 224),
                    "vehicle_impact": u32(payload, 228),
                    "can_rx_task_max_us": u32(payload, 232),
                    "uplink_pool_high_bytes": u32(payload, 236),
                    "usb_reconnect": u32(payload, 244),
                    "usb_forced_reset": u32(payload, 248),
                    "passive_violation": u32(payload, 252),
                    "capture_invalid_reason": u32(payload, 256),
                })
            if len(payload) >= 296 and payload[52] >= 7:
                self.last_health.update({
                    "host_absent_bus0_discard": u32(payload, 260),
                    "host_absent_bus1_discard": u32(payload, 264),
                    "host_absent_fifo_overflow": u32(payload, 268),
                    "host_absent_mcp_error": u32(payload, 272),
                    "host_absent_duration_ms": u32(payload, 276),
                    "passive_readback": u32(payload, 280),
                    "passive_readback_violation": u32(payload, 284),
                    "txreq_violation": u32(payload, 288),
                    "usb_dtr_change": u32(payload, 292),
                })
            if len(payload) >= 360 and payload[52] >= 8:
                self.last_health.update({
                    "publish_next": u64(payload, 296),
                    "usb_epoch": u32(payload, 312),
                    "usb_high": u32(payload, 316),
                    "usb_overflow": u32(payload, 320),
                    "usb_sent": u32(payload, 324),
                    "wifi_epoch": u32(payload, 328),
                    "wifi_high": u32(payload, 332),
                    "wifi_overflow": u32(payload, 336),
                    "wifi_sent": u32(payload, 340),
                    "wifi_connect": u32(payload, 344),
                    "wifi_disconnect": u32(payload, 348),
                    "wifi_stall_close": u32(payload, 352),
                    "no_sink_drop": u32(payload, 356),
                })
            if len(payload) >= 384 and payload[52] >= 9:
                self.last_health.update({
                    "wifi_socket_error": u32(payload, 360),
                    "wifi_send_budget_overrun": u32(payload, 364),
                    "wifi_send_call_max_us": u32(payload, 368),
                    "wifi_recv_call_max_us": u32(payload, 372),
                    "wifi_close_call_max_us": u32(payload, 376),
                    "main_loop_max_gap_us": u32(payload, 380),
                })
            if len(payload) >= 392 and payload[52] >= 10:
                self.last_health.update({
                    "reset_cause_bits": u32(payload, 384),
                    "reset_status_raw": u32(payload, 388),
                })
            if len(payload) >= 408 and payload[52] >= 11:
                self.last_health.update({
                    "previous_breadcrumb_valid": u32(payload, 392),
                    "previous_breadcrumb_stage": u32(payload, 396),
                    "previous_breadcrumb_uptime_ms": u32(payload, 400),
                    "previous_breadcrumb_sequence": u32(payload, 404),
                })
            if len(payload) >= 472 and payload[52] >= 12:
                profile_word = u32(payload, 464)
                integrity_word = u32(payload, 468)
                experiment_flags = (profile_word >> 24) & 0xFF
                self.last_health.update({
                    "recovery_flags": u32(payload, 408),
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
                    "watchdog_requested": (experiment_flags >> 3) & 1,
                    "watchdog_effective": experiment_flags & 1,
                    "watchdog_start_called": (experiment_flags >> 4) & 1,
                    "watchdog_start_succeeded": (experiment_flags >> 5) & 1,
                    "watchdog_timeout_matches": (experiment_flags >> 6) & 1,
                    "retained_valid_events": integrity_word & 0xFFFF,
                    "retained_corrupt_metadata": (integrity_word >> 16) & 0xFF,
                    "retained_corrupt_events": (integrity_word >> 24) & 0xFF,
                })
            if len(payload) >= 508 and payload[52] >= 13:
                call_flags = u32(payload, 484)
                self.last_health.update({
                    "runtime_contract_id32": u32(payload, 472),
                    "recovery_identity_id32": u32(payload, 476),
                    "watchdog_observed_timeout_ms": u32(payload, 480),
                    "previous_wifi_call_flags": call_flags,
                    "previous_wifi_call_valid": call_flags & 1,
                    "previous_wifi_call_in_progress": (call_flags >> 1) & 1,
                    "previous_wifi_call_completed": (call_flags >> 2) & 1,
                    "previous_wifi_call_contract_changed": (call_flags >> 3) & 1,
                    "previous_wifi_call_owner": (call_flags >> 8) & 0xFF,
                    "previous_wifi_call_operation": (call_flags >> 16) & 0xFF,
                    "previous_wifi_call_boot_sequence": u32(payload, 488),
                    "previous_wifi_call_sequence": u32(payload, 492),
                    "previous_wifi_call_started_ms": u32(payload, 496),
                    "previous_wifi_call_duration_us": u32(payload, 500),
                    "previous_wifi_call_result": i32(payload, 504),
                })

    def summary(self) -> str:
        parts = [
            f"typed_seq_gaps={self.typed_seq_gaps}",
            f"segment_seq_gaps={self.segment_seq_gaps}",
            f"capture_seq_gaps={self.capture_seq_gaps}",
            f"segment_decode_errors={self.segment_decode_errors}",
        ]
        for key in (
            "serial_clear",
            "serial_clear_bytes",
            "serial_backpressure",
            "serial_high_bytes",
            "serial_enqueue_fail",
            "segment_enqueue_fail",
            "can_rx_dropped_total",
            "can_fifo_overflow_total",
            "can_q_high",
            "mcp_drain_budget_hit",
            "mcp_spi_error",
            "mcp_error_flag",
            "mcp_last_canintf",
            "mcp_last_eflg",
            "mcp_last_canctrl",
            "mcp_last_int_low",
            "fw_profile",
            "vehicle_impact",
            "passive_violation",
            "can_rx_task_max_us",
            "uplink_pool_high_bytes",
            "pool_alloc_fail",
            "can_truth_pool_fail",
            "descriptor_high",
            "usb_reconnect",
            "usb_forced_reset",
            "capture_invalid_reason",
            "host_absent_bus0_discard",
            "host_absent_bus1_discard",
            "host_absent_fifo_overflow",
            "host_absent_mcp_error",
            "host_absent_duration_ms",
            "passive_readback",
            "passive_readback_violation",
            "txreq_violation",
            "usb_dtr_change",
            "usb_epoch",
            "usb_high",
            "usb_overflow",
            "usb_sent",
            "wifi_epoch",
            "wifi_high",
            "wifi_overflow",
            "wifi_sent",
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
            "reset_cause_bits",
            "reset_status_raw",
            "previous_breadcrumb_valid",
            "previous_breadcrumb_stage",
            "previous_breadcrumb_uptime_ms",
            "previous_breadcrumb_sequence",
            "recovery_flags",
            "firmware_source_id32",
            "boot_sequence",
            "consecutive_early_resets",
            "early_reset_total",
            "wifi_quarantine_total",
            "previous_boot_sequence",
            "previous_last_progress_id",
            "previous_last_progress_detail",
            "previous_last_progress_uptime_ms",
            "current_last_progress_id",
            "current_last_progress_detail",
            "current_last_progress_uptime_ms",
            "retained_event_sequence",
            "reset_experiment_selector",
            "requested_wifi_mode",
            "effective_wifi_mode",
            "reset_experiment_flags",
            "watchdog_requested",
            "watchdog_effective",
            "watchdog_start_called",
            "watchdog_start_succeeded",
            "watchdog_timeout_matches",
            "retained_valid_events",
            "retained_corrupt_metadata",
            "retained_corrupt_events",
        ):
            if key in self.last_health:
                parts.append(f"{key}={self.last_health[key]}")
        return "[SUMMARY] " + " ".join(parts)


def main():
    parser = argparse.ArgumentParser(description="Decode Portenta typed record stream.")
    parser.add_argument("--port", default="COM6")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--raw", action="store_true", help="Print every record, including high-rate CAN.")
    parser.add_argument("--seconds", type=float, default=0.0, help="Stop after this many seconds; 0 runs forever.")
    parser.add_argument("--max-records", type=int, default=0, help="Stop after this many printed records; 0 runs forever.")
    args = parser.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.1)
    ser.reset_input_buffer()

    buf = bytearray()
    last_can_print = 0.0
    printed = 0
    tracker = GapTracker()
    deadline = time.time() + args.seconds if args.seconds > 0 else None
    try:
        while True:
            if deadline is not None and time.time() >= deadline:
                break
            buf += ser.read(4096)
            while True:
                frame = parse_frame(buf)
                if frame is None:
                    break
                tracker.observe(frame)
                if frame.get("type") == 1 and not args.raw:
                    now = time.time()
                    if now - last_can_print < 0.5:
                        continue
                    last_can_print = now
                print(describe(frame))
                printed += 1
                if args.max_records > 0 and printed >= args.max_records:
                    raise StopIteration
    except (KeyboardInterrupt, StopIteration):
        pass
    finally:
        print(tracker.summary())


if __name__ == "__main__":
    main()
