import argparse
import ctypes
import datetime as dt
import json
import pathlib
import statistics
import time
from ctypes import byref, c_int, c_long, c_uint, c_ubyte


CAN_OK = 0
CAN_ERR_NOMSG = -2
CAN_BITRATE_500K = -2
CAN_OPEN_ACCEPT_VIRTUAL = 0x20
CANMSG_ERROR_FRAME = 0x20
CONTROL_IDS = (0x007,)


def status_text(canlib, status):
    buffer = ctypes.create_string_buffer(256)
    canlib.canGetErrorText(c_int(status), buffer, c_uint(len(buffer)))
    return buffer.value.decode(errors="replace")


def percentile(values, ratio):
    if not values:
        return None
    ordered = sorted(values)
    index = min(len(ordered) - 1, int((len(ordered) - 1) * ratio))
    return ordered[index]


def add_check(checks, name, passed, detail):
    checks.append({"name": name, "pass": bool(passed), "detail": detail})


def main():
    parser = argparse.ArgumentParser(
        description="Judge physical CSM remote-control CAN bursts with Kvaser."
    )
    parser.add_argument("--channel", type=int, default=0)
    parser.add_argument("--seconds", type=float, default=20.0)
    parser.add_argument("--artifact-dir", default="artifacts/remote_hil")
    parser.add_argument("--require-motion", action="store_true")
    parser.add_argument("--minimum-steering-span", type=int, default=40)
    args = parser.parse_args()

    canlib = ctypes.WinDLL("canlib32.dll")
    canlib.canInitializeLibrary()
    channel_count = c_int()
    status = canlib.canGetNumberOfChannels(byref(channel_count))
    if status != CAN_OK:
        raise SystemExit(
            f"canGetNumberOfChannels failed {status}: {status_text(canlib, status)}"
        )
    if args.channel < 0 or args.channel >= channel_count.value:
        raise SystemExit(
            f"Kvaser channel {args.channel} is outside 0..{channel_count.value - 1}"
        )

    handle = canlib.canOpenChannel(c_int(args.channel), c_int(CAN_OPEN_ACCEPT_VIRTUAL))
    if handle < 0:
        raise SystemExit(f"canOpenChannel failed {handle}: {status_text(canlib, handle)}")

    frames = []
    read_errors = []
    try:
        status = canlib.canSetBusParams(
            c_int(handle), c_long(CAN_BITRATE_500K),
            c_uint(0), c_uint(0), c_uint(0), c_uint(0), c_uint(0)
        )
        if status != CAN_OK:
            raise SystemExit(f"canSetBusParams failed {status}: {status_text(canlib, status)}")
        status = canlib.canBusOn(c_int(handle))
        if status != CAN_OK:
            raise SystemExit(f"canBusOn failed {status}: {status_text(canlib, status)}")

        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            can_id = c_long()
            data_buffer = (c_ubyte * 8)()
            dlc = c_uint()
            flags = c_uint()
            timestamp = c_uint()
            status = canlib.canReadWait(
                c_int(handle), byref(can_id), data_buffer, byref(dlc),
                byref(flags), byref(timestamp), c_uint(100)
            )
            if status == CAN_ERR_NOMSG:
                continue
            if status != CAN_OK:
                read_errors.append({"status": status, "text": status_text(canlib, status)})
                break
            data = bytes(data_buffer[: min(dlc.value, 8)])
            frames.append({
                "timestamp_ms": timestamp.value,
                "can_id": can_id.value,
                "dlc": dlc.value,
                "flags": flags.value,
                "data": data.hex(),
            })
    finally:
        canlib.canBusOff(c_int(handle))
        canlib.canClose(c_int(handle))

    error_frames = [
        frame for frame in frames if frame["flags"] & CANMSG_ERROR_FRAME
    ]
    control_frames = [
        frame for frame in frames
        if not (frame["flags"] & CANMSG_ERROR_FRAME)
        and frame["can_id"] in CONTROL_IDS
    ]
    by_id = {can_id: [] for can_id in CONTROL_IDS}
    for frame in control_frames:
        by_id[frame["can_id"]].append(frame)

    checks = []
    add_check(checks, "kvaser_read", not read_errors,
              f"total={len(frames)} errors={read_errors}")
    add_check(checks, "can_error_frames", not error_frames,
              f"count={len(error_frames)}")
    stats = {}
    for can_id in CONTROL_IDS:
        observed = by_id[can_id]
        intervals = [
            (observed[index]["timestamp_ms"] - observed[index - 1]["timestamp_ms"])
            & 0xFFFFFFFF
            for index in range(1, len(observed))
        ]
        median_ms = statistics.median(intervals) if intervals else None
        p95_ms = percentile(intervals, 0.95)
        stats[f"0x{can_id:X}"] = {
            "count": len(observed),
            "period_median_ms": median_ms,
            "period_p95_ms": p95_ms,
            "period_max_ms": max(intervals) if intervals else None,
            "last_data": observed[-1]["data"] if observed else None,
        }
        period_ok = (
            len(observed) >= 10 and intervals and
            15 <= median_ms <= 25 and p95_ms <= 30 and max(intervals) <= 100
        )
        add_check(checks, f"period_0x{can_id:X}", period_ok,
                  f"count={len(observed)} median={median_ms} p95={p95_ms} "
                  f"max={max(intervals) if intervals else None}")
        add_check(checks, f"dlc_0x{can_id:X}",
                  bool(observed) and all(frame["dlc"] == 8 for frame in observed),
                  f"dlc={sorted(set(frame['dlc'] for frame in observed))}")

    steering_values = []
    auxiliary_values = []
    payload_ok = True
    for frame in by_id[0x007]:
        data = bytes.fromhex(frame["data"])
        if len(data) == 8:
            steering_values.append(data[0])
            auxiliary_values.append(data[7])
            payload_ok = payload_ok and 10 <= data[0] <= 250
            payload_ok = payload_ok and all(value == 0 for value in data[1:7])
            payload_ok = payload_ok and data[7] in (0x00, 0x01, 0x80)
    add_check(checks, "remote_payload_contract", bool(steering_values) and payload_ok,
              f"frames={len(steering_values)} steering="
              f"{min(steering_values) if steering_values else None}.."
              f"{max(steering_values) if steering_values else None} "
              f"aux={sorted(set(auxiliary_values))}")

    motion = {
        "steering_byte_min": min(steering_values) if steering_values else None,
        "steering_byte_max": max(steering_values) if steering_values else None,
        "auxiliary_values": sorted(set(auxiliary_values)),
    }
    if args.require_motion:
        steering_span = max(steering_values) - min(steering_values) \
            if steering_values else 0
        add_check(checks, "steering_motion",
                  steering_span >= args.minimum_steering_span,
                  f"span={steering_span} range="
                  f"{motion['steering_byte_min']}.."
                  f"{motion['steering_byte_max']}")

    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    artifact_dir = pathlib.Path(args.artifact_dir)
    artifact_dir.mkdir(parents=True, exist_ok=True)
    artifact_path = artifact_dir / f"kvaser_remote_hil_{stamp}.json"
    result = {
        "captured_at": stamp,
        "channel": args.channel,
        "seconds": args.seconds,
        "total_frames": len(frames),
        "control_frames": len(control_frames),
        "error_frames": len(error_frames),
        "stats": stats,
        "motion": motion,
        "read_errors": read_errors,
        "checks": checks,
        "frames": frames,
    }
    result["pass"] = bool(checks) and all(check["pass"] for check in checks)
    artifact_path.write_text(
        json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8"
    )
    print(f"verdict={'PASS' if result['pass'] else 'FAIL'}")
    for check in checks:
        print(f"{'PASS' if check['pass'] else 'FAIL'} {check['name']}: {check['detail']}")
    print(f"json={artifact_path.resolve()}")
    raise SystemExit(0 if result["pass"] else 2)


if __name__ == "__main__":
    main()
