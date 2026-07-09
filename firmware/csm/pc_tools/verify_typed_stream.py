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
    15: "HOST_CLEAR_FAULT_LOCKOUT",
    16: "CAN_RX_SEGMENT",
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
}

BUS_TRANSCEIVER_NAMES = {
    1: "TJA1050",
    2: "TJA1051",
    3: "MidCarrierU2",
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
}


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


def i64(payload: bytes, offset: int) -> int:
    return struct.unpack_from("<q", payload, offset)[0]


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

    if rtype == 16 and len(payload) >= 32:
        segment_seq = u64(payload, 0)
        first_capture_seq = u64(payload, 8)
        frame_count = u16(payload, 16)
        entry_size = payload[18]
        dropped_before = u32(payload, 20)
        fifo_before = u32(payload, 24)
        preview = []
        if entry_size >= 30 and len(payload) >= 32 + frame_count * entry_size:
            for index in range(min(frame_count, 3)):
                offset = 32 + index * entry_size
                capture_seq = u64(payload, offset)
                mono = u64(payload, offset + 8)
                can_id_flags = u32(payload, offset + 16)
                can_id = can_id_flags & 0x1FFFFFFF
                dlc = payload[offset + 20] & 0x0F
                bus = payload[offset + 21]
                data = payload[offset + 22 : offset + 30]
                preview.append(
                    f"#{index} cap={capture_seq} t={mono} bus={bus} id=0x{can_id:X} dlc={dlc} data={data.hex(' ')}"
                )
        return (
            f"[{name}] seq={seq} segment_seq={segment_seq} first_capture_seq={first_capture_seq} "
            f"frames={frame_count} entry={entry_size} dropped_before={dropped_before} fifo_before={fifo_before} "
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
        if len(payload) >= 128 and payload[52] in (2, 4, 5, 6):
            extra = (
                f" health_v={payload[52]} safety_v2={payload[54]} fault_bits=0x{payload[55]:02X}"
                f" heartbeat_age_ms={u32(payload, 56)} lease_ms={u32(payload, 60)}"
                f" host_crc={u32(payload, 64)} host_req={u32(payload, 68)}"
                f" host_acc={u32(payload, 72)} host_rej={u32(payload, 76)}"
                f" mcp_tx={u32(payload, 80)} mcp_fail={u32(payload, 84)}"
                f" builtin_tx={u32(payload, 88)} builtin_fail={u32(payload, 92)}"
                f" mcp_spi_err={u32(payload, 96)} mcp_err_flags={u32(payload, 100)}"
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

    return f"[{name}] seq={seq} len={len(payload)} payload={payload.hex(' ')}"


class GapTracker:
    def __init__(self):
        self.last_typed_seq = None
        self.typed_seq_gaps = 0
        self.last_segment_seq = None
        self.segment_seq_gaps = 0
        self.last_capture_seq = None
        self.capture_seq_gaps = 0
        self.last_health = {}

    @staticmethod
    def _seq16_gap(prev: int, cur: int) -> int:
        expected = (prev + 1) & 0xFFFF
        if cur == expected:
            return 0
        return (cur - expected) & 0xFFFF

    def observe(self, frame):
        if frame.get("bad_crc"):
            return

        seq = frame["seq"]
        if self.last_typed_seq is not None:
            self.typed_seq_gaps += self._seq16_gap(self.last_typed_seq, seq)
        self.last_typed_seq = seq

        rtype = frame["type"]
        payload = frame["payload"]
        if rtype == 16 and len(payload) >= 32:
            segment_seq = u64(payload, 0)
            if self.last_segment_seq is not None and segment_seq != self.last_segment_seq + 1:
                self.segment_seq_gaps += max(0, segment_seq - self.last_segment_seq - 1)
            self.last_segment_seq = segment_seq

            frame_count = u16(payload, 16)
            entry_size = payload[18]
            if entry_size >= 30 and len(payload) >= 32 + frame_count * entry_size:
                for index in range(frame_count):
                    offset = 32 + index * entry_size
                    capture_seq = u64(payload, offset)
                    if self.last_capture_seq is not None and capture_seq != self.last_capture_seq + 1:
                        self.capture_seq_gaps += max(0, capture_seq - self.last_capture_seq - 1)
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

    def summary(self) -> str:
        parts = [
            f"typed_seq_gaps={self.typed_seq_gaps}",
            f"segment_seq_gaps={self.segment_seq_gaps}",
            f"capture_seq_gaps={self.capture_seq_gaps}",
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
