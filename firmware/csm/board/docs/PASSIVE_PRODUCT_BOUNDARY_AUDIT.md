# CSM Passive Product Boundary Audit

This document is the firmware-side acceptance boundary for the two-bus Passive
Product CSM.

## Required Runtime Data Flow

```text
vehicle CAN bus0/bus1
  -> USB power-up with CAN front-end initialization held
  -> if host absent: no typed payload staging
  -> if host session open: clear stale payload, wait quiet window, initialize CAN front-end, enable ACK-observe
  -> CAN_RX_SEGMENT evidence for new frames only
  -> CDC evidence uplink
```

Host absent frames are outside the VSM capture session. They must not be staged
and replayed when USB reconnects.

## Session Contract

- `host_session_epoch` increments once per new CDC/DTR host session.
- `transport_epoch` increments with the uplink session epoch.
- Session close discards queued/staged uplink payload.
- Session open discards stale uplink payload, resets the segment builder epoch,
  emits `CAPABILITY`, emits USB session evidence, emits host-absent summary if
  present, then emits board health.
- `USB_ATTACH_QUARANTINE` is CDC/uplink/session cleanup only. It must not replay
  old payload or enable host TX/control. Passive field firmware also defers CAN
  front-end initialization until the session quiet window has elapsed; it may
  transition to ACK-observe only after that initialization succeeds.

## Passive Proof Boundary

`CAPABILITY v6` hardware fields are claims and artifact references:

- silent strapped;
- galvanic isolated;
- power-off passive;
- reset safe;
- TXD gated;
- host TX/control drive path not populated or physically inaccessible;
- field SKU ID;
- external analyzer artifact ID;
- hotplug pass count.

They are not proof by themselves. Vehicle-impact-free PASS requires external
analyzer/scope/DTC evidence and matching artifact IDs.

## Two-Bus Product Rule

The current product is two-bus ACK-capable observe-only. One-bus passive artifacts are invalid,
but mismatch diagnostics must remain visible so wrong upload or wiring mistakes
are not hidden.

## Firmware Guard Rule

Passive builds must fail if they link or enable host downlink, host CAN TX,
control TX, test TX, or USB reconnect reset. Normal/ACK-capable mode is allowed
only inside the session-stable ACK-observe state machine. Passive builds must
also define `BOARD_PASSIVE_DEFER_CAN_FRONTEND_INIT_UNTIL_SESSION=1`.
