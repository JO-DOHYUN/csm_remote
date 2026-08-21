# CURRENT


## Runtime Semantic Baselines

- CSM: `5c9f6f71ebb06320f8835c4115ddde03314a9762`
- Android: `fea18d9d5660d96cfa67e93e7e900ef9cceb62a6`
- Feeder: `NOT IN SCOPE / UNRESOLVED`

## Product Phase

`ACTIVE ARCHITECTURE / EXPLORATORY QUALIFICATION / RELEASE BLOCKED`

## Active Architecture IDs

- Cross-repository active manifest: `csm-d038_android-d043`
- Semantic/raw boundary lineage: CSM D-035, Android D-040
- HW-execution/qualification lineage: CSM D-038, Android D-043

## Verified Current Facts

- Active manifest and source select one Service/HIL M7 environment and one M4 frontend environment.
- Service/HIL Host requests have no CSM software execution FIFO, cadence lane, hidden retry, or replay.
- `CONTROL_ACK`, terminal `CONTROL_TX_EVIDENCE`, and physical `CAN_TX_RAW` remain distinct evidence.
- Production Android control is disabled; explicit Service/HIL control is enabled.
- Enabled steady schema source calculation is `1,095 records/s`, `131,513 B/s`.

## Current Failing Gate

Timing and transient thresholds are exploratory. Product constants are not frozen, so qualification
HIL and release PASS are blocked.

## Open Risks

- External-analyzer timing, cancel/preemption, bus-error, exact-N EHB and long-soak evidence is open.
- Feeder baseline and paired deployment identity are unresolved in this migration.
- Existing local `platformio.ini` change is user-owned and excluded from this harness migration.

## Active Experiments

- `host-threshold-qualification`: Host sender-time, pending/completion, pressure and
  cancellation latency measurement (`docs/experiments/host_threshold_qualification.yaml`).
- `production_authority: false`

## Next Required Verification

Freeze reviewed constants only after exploratory measurement, then run the affected host/native
contracts, exact M4/M7 builds, Android tests/device gate, and four-window external CAN qualification.
