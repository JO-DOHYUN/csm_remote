"""Offline bounded decoder for the correlated causal-ACK bench capture."""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "firmware/csm/pc_tools"))
from control_path_diagnostic import decode_control_path_diagnostic  # noqa: E402
from verify_typed_stream import parse_frame  # noqa: E402


def u32(data, offset):
    return int.from_bytes(data[offset:offset + 4], "little")


parser = argparse.ArgumentParser()
parser.add_argument("capture", type=Path)
parser.add_argument("--heartbeat", type=int, required=True)
parser.add_argument("--tail", type=int, default=0)
parser.add_argument("--epochs", action="store_true")
args = parser.parse_args()
buffer = bytearray(args.capture.read_bytes())
diagnostics = []
acks = []
bad_crc = 0
while buffer:
    before = len(buffer)
    frame = parse_frame(buffer)
    if frame is None:
        if len(buffer) == before:
            break
        continue
    if frame.get("bad_crc"):
        bad_crc += 1
    elif frame["type"] == 27:
        decoded = decode_control_path_diagnostic(frame["payload"])
        if decoded:
            diagnostics.append(decoded)
    elif frame["type"] == 6 and len(frame["payload"]) >= 28:
        acks.append((int.from_bytes(frame["payload"][0:8], "little"), u32(frame["payload"], 8),
                     frame["payload"][12], frame["payload"][13]))

print(f"frames: diagnostics={len(diagnostics)} acks={len(acks)} bad_crc={bad_crc}")
if args.epochs:
    for item in diagnostics:
        print(f"epoch_sample mono_us={item.get('mono_us')} epoch={item.get('ControlEpoch')} "
              f"heartbeat={item.get('HeartbeatId')} generated={item.get('AckGeneratedId')} "
              f"sent={item.get('AckSentId')} queued={item.get('AckQueued')} "
              f"would_block={item.get('ControlWouldBlock')} close={item.get('ControlCloseTotal')}")
for mono, command, status, reason in acks:
    if abs(command - args.heartbeat) <= 3:
        print(f"usb_ack mono_us={mono} id={command} status={status} reason={reason}")
keys = (
    "mono_us", "ControlEpoch", "HeartbeatId", "HeartbeatRxMs", "HeartbeatAckRef",
    "AckGeneratedId", "AckGeneratedMs", "AckOfferedTotal", "AckAdmittedTotal",
    "AckRejectedTotal", "AckQueued", "AckSentId", "AckSentMs", "AckSentTotal",
    "ControlTxBytes", "ControlWouldBlock", "ControlSendMaxUs", "ControlCloseTotal",
    "ControlCloseReason", "ControlCloseResult", "M7FirstReason", "M7FirstMs",
    "M7FirstControlEpoch", "M7FirstHeartbeatId", "M7FirstAckGenId",
    "M7FirstAckSentId", "M7FirstRxBytes", "TransportFirstReason",
    "TransportFirstMs", "TransportFirstAckGenId", "TransportFirstAckSentId",
    "TransportFirstResult", "ControlTxPendingId", "ControlTxOffset",
)
selected = [item for item in diagnostics if (
    abs(item.get("HeartbeatId", 0) - args.heartbeat) <= 3 or
    item.get("M7FirstHeartbeatId") == args.heartbeat
)]
if args.tail:
    selected.extend(diagnostics[-args.tail:])
seen = set()
for item in selected:
    identity = (item.get("mono_us"), item.get("HeartbeatId"))
    if identity in seen:
        continue
    seen.add(identity)
    print("diagnostic " + " ".join(f"{key}={item.get(key)}" for key in keys))
