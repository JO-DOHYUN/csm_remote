---
authority: PROCEDURE
owner: verification
status: active
read_when: ARM or steering endurance loses health observation or causal ACK
---

# One-run control-path attribution

No physical root cause is established by an Android error string. Keep the
failed state; do not reset before recovering the first snapshots. Observe-only
capture still follows existing write-lock/preflight. Software changes here do
not authorize upload, CAN injection or vehicle motion.

## Minimum evidence

- paired M7/M4 source/runtime/build identities and APK SHA; M7 boot/control epoch
- canonical typed capture including 26 (local readiness) and 27 (all 82 fields)
  plus existing TRANSPORT_DIAGNOSTIC/BOARD_HEALTH/RUNTIME_DIAGNOSTIC
- Android `VSM_CONTROL_FIRST_FAILURE`, pending heartbeat ID, HostControlPlane
  write start/end/epoch, receive/ACK times and IDs (same Android monotonic clock)
- independent USB observer during Wi-Fi loss; if unavailable, recover record 27
  on reconnect without reboot. If M7 never resumes, SWD/read-only retained
  runtime breadcrumbs and M4 state are required; reconnect alone is not proof.
- external J4 CAN1 steering 0x007 cadence/terminal evidence when execution itself
  must be distinguished from frozen M4 foreground/health. No diagnostic upload
  or source success substitutes for this physical measurement.

## Decision order

| Boundary | Evidence and interpretation |
|---|---|
| M4 execution | TIM4 and steering terminal progress, FDCAN first fault, external J4 trace. Other lanes progressing never clears steering stall. |
| Health generation/publication | M4HealthAttempts, M4TraceTim4Tick, M4SnapshotRejects, M4HealthPublished/PublishFailures. Trace advances independently of normal health snapshot success. |
| IPC/read | M7ReadAttempts/Rejects, raw record-26 reject detail/health sequence and M7HealthAgeMs. Publication grows but M7 cannot accept it: IPC/read boundary. |
| M7 local truth/runtime | LocalReadyReason, ServiceGapMaxMs and M7First context; reason 3 is M7-local age, not Android arrival age. |
| Android TX -> CSM RX | Pending heartbeat ID with successful/unfinished Android write; ControlRxBytes/RxLastMs/ReadBytes and HeartbeatId/RxMs/AckRef. RX received but unprocessed indicates mailbox/parser/M7 service interval, not return transport. |
| ACK generation/admission | AckGeneratedId/Ms, offered/admitted/rejected totals and queue/high-water. Full queue or inactive epoch is not successful TX. |
| ACK socket TX -> Android | AckSentId/Ms/Total only advances after whole frame socket acceptance; pending ID/offset, WouldBlock/send duration/close reason show local TX failure. Sent without Android receipt leaves return transport/Android receive boundary unresolved until capture, not proven radio loss. |
| Telemetry/control common failure | Compare M7First and TransportFirst in the same M7 clock, worker call/age, telemetry epoch/bytes, arena allocation failures and M7 service gap. Coincidence alone is not causality. Healthy local/causal progress with only telemetry gaps disproves M4 failure for that interval. |

M7 first failure starts observing only after explicit accepted ARM. Both CSM
first snapshots persist until boot, including normal DISARM/re-ARM/reconnect.
Use a baseline snapshot to detect a pre-existing latch before reproduction.
First timestamps are **first observations**, not an invented total ordering of
physical events. Independent atomic counters may be sampled at slightly
different instants. Compare modulo-u32 deltas only within one boot/epoch and
do not subtract Android and M7 clocks. Decode `*Result` as two's-complement i32
for NSAPI errors; Android write success is `-1 pending / 0 failed / 1 written`.
Use Android's existing raw capture while it owns the telemetry connection;
do not introduce a competing TCP monitor during reproduction. USB is the
independent observer; the TCP monitor can recover the retained record afterward.

Telemetry-only observation gaps do not stop ACTIVE. The endurance result keeps
`evidenceComplete=false` even after recovery/normal completion; it is not a
fully witnessed endurance PASS. Causal ACK timeout and CSM-local watchdog/
authority rules remain unchanged. No timeout/queue/retry expansion was made.

## Software gates

CSM H0/L1/L2/L3/Wi-Fi guards; remote-control and Wi-Fi control-plane native
contracts; uplink contract; `tools/verify_control_path_contract.py` (82 canonical
fields, source producer, strict Python decoder). Android H0/L2/integration,
runtime/runtime-api/Service-HIL/UI tests and Observer/Service-HIL builds. Exact
paired firmware environments follow DEVELOPMENT_SETUP_KO.md.
