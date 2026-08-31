import argparse
import datetime as dt
import json
import pathlib
import statistics
import struct
import time

import serial

from verify_typed_stream import parse_frame, u16, u32, u64, i16


CONTROL_IDS = (0x007,)
UNKNOWN_AGE = 0xFFFFFFFF


def percentile(values, ratio):
    if not values:
        return None
    ordered = sorted(values)
    index = min(len(ordered) - 1, int((len(ordered) - 1) * ratio))
    return ordered[index]


def remote_state(payload):
    if len(payload) < 228:
        return None
    return {
        "schema": payload[8],
        "link_state": payload[9],
        "authority_state": payload[10],
        "active_source": payload[11],
        "flags": payload[12],
        "link_quality": payload[13],
        "rssi_dbm_magnitude": payload[14],
        "last_crsf_type": payload[15],
        "m4_boot_id": u32(payload, 16),
        "shared_sequence": u32(payload, 20),
        "mailbox_age_ms": u32(payload, 24),
        "drive_permille": i16(payload, 28),
        "steering_permille": i16(payload, 30),
        "raw_ch2": u16(payload, 32),
        "raw_ch4": u16(payload, 34),
        "uart_baud": u32(payload, 36),
        "rx_bytes": u32(payload, 40),
        "valid_frames": u32(payload, 44),
        "rc_frames": u32(payload, 48),
        "link_frames": u32(payload, 52),
        "rejected_length": u32(payload, 56),
        "rejected_crc": u32(payload, 60),
        "inter_byte_resets": u32(payload, 64),
        "mailbox_publishes": u32(payload, 68),
        "telemetry_tx_frames": u32(payload, 72),
        "telemetry_tx_bytes": u32(payload, 76),
        "serial_write_failures": u32(payload, 80),
        "control_cycles": u32(payload, 84),
        "neutral_cycles": u32(payload, 88),
        "cycle_deadline_misses": u32(payload, 92),
        "can_tx_success": u32(payload, 96),
        "can_tx_failed": u32(payload, 100),
        "ipc_rejects": u32(payload, 104),
        "decision": payload[108],
        "last_address": payload[109],
        "sample_state": payload[110],
        "last_ipc_reject_detail": payload[111],
        "cycle_period_ms": u16(payload, 112),
        "frame_gap_ms": u16(payload, 114),
        "channels_permille": [i16(payload, 128 + i * 2) for i in range(16)],
        "link_statistics_valid": payload[160],
        "uplink_rssi_ant1": payload[161],
        "uplink_rssi_ant2": payload[162],
        "uplink_snr": struct.unpack_from("<b", payload, 163)[0],
        "active_antenna": payload[164],
        "rf_profile": payload[165],
        "uplink_rf_power": payload[166],
        "downlink_rssi": payload[167],
        "downlink_link_quality": payload[168],
        "downlink_snr": struct.unpack_from("<b", payload, 169)[0],
        "shared_publish_failures": u32(payload, 170),
        "raw_channels": [u16(payload, 176 + i * 2) for i in range(16)],
        "accepted_rc_frames": u32(payload, 208),
        "normalization_rejects": u32(payload, 212),
        "last_rc_age_ms": u32(payload, 216),
        "last_link_statistics_age_ms": u32(payload, 220),
        "last_normalize_reject_detail": u16(payload, 224),
    }


def counter_delta(first, last, key):
    return (last[key] - first[key]) & 0xFFFFFFFF


def add_check(checks, name, passed, detail):
    checks.append({"name": name, "pass": bool(passed), "detail": detail})


def main():
    parser = argparse.ArgumentParser(
        description="Capture and judge the CSM R16SM/CRSF remote-control vertical slice."
    )
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seconds", type=float, default=20.0)
    parser.add_argument("--artifact-dir", default="artifacts/remote_hil")
    parser.add_argument("--receive-only", action="store_true")
    parser.add_argument("--require-motion", action="store_true")
    parser.add_argument("--minimum-raw-span", type=int, default=300)
    args = parser.parse_args()

    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    artifact_dir = pathlib.Path(args.artifact_dir)
    artifact_dir.mkdir(parents=True, exist_ok=True)
    raw_path = artifact_dir / f"remote_hil_{stamp}.typed.bin"
    json_path = artifact_dir / f"remote_hil_{stamp}.json"

    raw_capture = bytearray()
    parse_buffer = bytearray()
    states = []
    typed_seq_last = None
    typed_seq_gaps = 0
    bad_crc = 0
    record_counts = {}
    can_times = {can_id: [] for can_id in CONTROL_IDS}
    can_last = {}
    m4_boot_ids = set()
    boot_session_ids = set()
    board_boot_events = 0
    capability = {}

    deadline = time.monotonic() + args.seconds
    with serial.Serial(args.port, args.baud, timeout=0.05) as ser:
        ser.reset_input_buffer()
        while time.monotonic() < deadline:
            chunk = ser.read(8192)
            if not chunk:
                continue
            raw_capture.extend(chunk)
            parse_buffer.extend(chunk)
            while True:
                frame = parse_frame(parse_buffer)
                if frame is None:
                    break
                if frame.get("bad_crc"):
                    bad_crc += 1
                    continue
                seq = frame["seq"]
                if typed_seq_last is not None:
                    expected = (typed_seq_last + 1) & 0xFFFF
                    if seq != expected:
                        typed_seq_gaps += (seq - expected) & 0xFFFF
                typed_seq_last = seq
                rtype = frame["type"]
                payload = frame["payload"]
                record_counts[rtype] = record_counts.get(rtype, 0) + 1

                if rtype == 18:
                    state = remote_state(payload)
                    if state is not None:
                        states.append(state)
                        m4_boot_ids.add(state["m4_boot_id"])
                elif rtype == 2 and len(payload) >= 30:
                    can_id = u32(payload, 8) & 0x1FFFFFFF
                    if can_id in can_times:
                        can_times[can_id].append(u64(payload, 0))
                        can_last[can_id] = {
                            "bus": payload[13],
                            "dlc": payload[12] & 0x0F,
                            "data": payload[14:22].hex(" "),
                        }
                elif rtype == 17 and len(payload) >= 32:
                    boot_session_ids.add(u64(payload, 8))
                elif rtype == 7 and len(payload) >= 16 and u16(payload, 8) == 1:
                    board_boot_events += 1
                elif rtype == 9 and len(payload) >= 224:
                    capability = {
                        "firmware_dirty": payload[113],
                        "firmware_profile": payload[192],
                        "host_command_rx": payload[195],
                        "control_path": payload[196],
                    }

    raw_path.write_bytes(raw_capture)
    checks = []
    add_check(checks, "typed_crc", bad_crc == 0, f"bad_crc={bad_crc}")
    add_check(checks, "typed_sequence", typed_seq_gaps == 0,
              f"typed_seq_gaps={typed_seq_gaps}")
    add_check(checks, "remote_records", len(states) >= 10,
              f"records={len(states)}")

    result = {
        "captured_at": stamp,
        "port": args.port,
        "seconds": args.seconds,
        "raw_artifact": str(raw_path.resolve()),
        "record_counts": record_counts,
        "bad_crc": bad_crc,
        "typed_seq_gaps": typed_seq_gaps,
        "board_boot_events": board_boot_events,
        "boot_session_ids": [f"0x{x:016X}" for x in sorted(boot_session_ids)],
        "m4_boot_ids": [f"0x{x:08X}" for x in sorted(m4_boot_ids)],
        "capability": capability,
        "can": {},
        "checks": checks,
    }

    if states:
        first = states[0]
        last = states[-1]
        result["first_remote_state"] = first
        result["last_remote_state"] = last
        ch2_values = [state["raw_ch2"] for state in states]
        ch4_values = [state["raw_ch4"] for state in states]
        result["channel_ranges"] = {
            "ch2": {"min": min(ch2_values), "max": max(ch2_values)},
            "ch4": {"min": min(ch4_values), "max": max(ch4_values)},
        }
        add_check(checks, "remote_schema", last["schema"] == 3,
                  f"schema={last['schema']}")
        add_check(checks, "configured_crsf_baud", last["uart_baud"] == 416666,
                  f"configured_baud={last['uart_baud']} physical_bit_time=NOT_MEASURED")
        add_check(checks, "crsf_rx_progress",
                  counter_delta(first, last, "rx_bytes") > 0 and
                  counter_delta(first, last, "accepted_rc_frames") > 0,
                  f"rx_delta={counter_delta(first, last, 'rx_bytes')} "
                  f"accepted_delta={counter_delta(first, last, 'accepted_rc_frames')}")
        add_check(checks, "crsf_integrity",
                  counter_delta(first, last, "rejected_length") == 0 and
                  counter_delta(first, last, "rejected_crc") == 0 and
                  counter_delta(first, last, "inter_byte_resets") == 0 and
                  counter_delta(first, last, "normalization_rejects") == 0,
                  f"len={counter_delta(first, last, 'rejected_length')} "
                  f"crc={counter_delta(first, last, 'rejected_crc')} "
                  f"gap={counter_delta(first, last, 'inter_byte_resets')} "
                  f"normalize={counter_delta(first, last, 'normalization_rejects')}")
        add_check(checks, "r16sm_address",
                  last["last_address"] == 0xC8,
                  f"address=0x{last['last_address']:02X} "
                  f"last_type=0x{last['last_crsf_type']:02X}")
        flags = last["flags"]
        add_check(checks, "remote_live", (flags & 0x0A) == 0x0A,
                  f"flags=0x{flags:02X} link={last['link_state']} "
                  f"sample={last['sample_state']} rc_age={last['last_rc_age_ms']}")
        link_statistics_observed = last["link_frames"] > 0
        link_statistics_ok = (
            not link_statistics_observed or
            (last["link_statistics_valid"] == 1 and
             0 < last["link_quality"] <= 100 and
             last["last_link_statistics_age_ms"] <= 500)
        )
        add_check(checks, "optional_link_statistics_veto", link_statistics_ok,
                  f"observed={link_statistics_observed} "
                  f"valid={last['link_statistics_valid']} lq={last['link_quality']} "
                  f"age={last['last_link_statistics_age_ms']} "
                  f"rssi=-{last['rssi_dbm_magnitude']}dBm")
        add_check(checks, "crsf_telemetry_tx",
                  counter_delta(first, last, "telemetry_tx_frames") > 0 and
                  counter_delta(first, last, "serial_write_failures") == 0,
                  f"frames={counter_delta(first, last, 'telemetry_tx_frames')} "
                  f"fail={counter_delta(first, last, 'serial_write_failures')}")
        add_check(checks, "m4_stable", len(m4_boot_ids) == 1,
                  f"m4_boot_ids={len(m4_boot_ids)}")
        add_check(checks, "runtime_integrity",
                  counter_delta(first, last, "cycle_deadline_misses") == 0 and
                  counter_delta(first, last, "can_tx_failed") == 0 and
                  counter_delta(first, last, "ipc_rejects") == 0 and
                  counter_delta(first, last, "shared_publish_failures") == 0,
                  f"deadline={counter_delta(first, last, 'cycle_deadline_misses')} "
                  f"can_fail={counter_delta(first, last, 'can_tx_failed')} "
                  f"ipc={counter_delta(first, last, 'ipc_rejects')} "
                  f"ipc_detail={last['last_ipc_reject_detail']} "
                  f"publish_fail={counter_delta(first, last, 'shared_publish_failures')}")
        if args.require_motion:
            ch2_span = max(ch2_values) - min(ch2_values)
            ch4_span = max(ch4_values) - min(ch4_values)
            add_check(checks, "ch2_motion", ch2_span >= args.minimum_raw_span,
                      f"span={ch2_span} min={min(ch2_values)} max={max(ch2_values)}")
            add_check(checks, "ch4_motion", ch4_span >= args.minimum_raw_span,
                      f"span={ch4_span} min={min(ch4_values)} max={max(ch4_values)}")

    for can_id, timestamps in can_times.items():
        intervals_ms = [
            (timestamps[i] - timestamps[i - 1]) / 1000.0
            for i in range(1, len(timestamps))
        ]
        stats = {
            "count": len(timestamps),
            "last": can_last.get(can_id),
            "period_median_ms": statistics.median(intervals_ms) if intervals_ms else None,
            "period_p95_ms": percentile(intervals_ms, 0.95),
            "period_max_ms": max(intervals_ms) if intervals_ms else None,
        }
        result["can"][f"0x{can_id:X}"] = stats
        if not args.receive_only:
            period_ok = (
                len(timestamps) >= 10 and intervals_ms and
                15.0 <= stats["period_median_ms"] <= 25.0 and
                stats["period_p95_ms"] <= 30.0
            )
            add_check(checks, f"can_0x{can_id:X}_period", period_ok,
                      f"count={len(timestamps)} median={stats['period_median_ms']} "
                      f"p95={stats['period_p95_ms']} max={stats['period_max_ms']}")

    if not args.receive_only:
        remote_frame = can_last.get(0x007)
        remote_payload_ok = False
        if remote_frame is not None:
            data = bytes.fromhex(remote_frame["data"])
            remote_payload_ok = (
                remote_frame["bus"] == 1 and remote_frame["dlc"] == 8 and
                len(data) == 8 and 10 <= data[0] <= 250 and
                all(value == 0 for value in data[1:7]) and
                data[7] in (0x00, 0x01, 0x80)
            )
        add_check(checks, "remote_can_identity", remote_payload_ok,
                  f"0x007={remote_frame}")

    result["pass"] = bool(checks) and all(check["pass"] for check in checks)
    json_path.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"verdict={'PASS' if result['pass'] else 'FAIL'}")
    for check in checks:
        print(f"{'PASS' if check['pass'] else 'FAIL'} {check['name']}: {check['detail']}")
    print(f"raw={raw_path.resolve()}")
    print(f"json={json_path.resolve()}")
    raise SystemExit(0 if result["pass"] else 2)


if __name__ == "__main__":
    main()
