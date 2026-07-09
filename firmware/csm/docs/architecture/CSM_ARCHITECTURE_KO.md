# CSM Architecture

## Product Identity

CSM is not a USB-CAN bridge in the product path. CSM Passive Product is a
two-bus ACK-capable observe-only CAN front-end that emits typed evidence over
CDC when a host session is present. Host-originated TX/control belongs only to
explicit bench/lab profiles.

## Product Modes

| Mode | Purpose | Vehicle passive acceptance | CAN TX/ACK |
| --- | --- | --- | --- |
| Passive Product | two-bus field monitor/logger evidence | allowed only after hardware evidence | ACK-observe, no host TX/control |
| Full Instrumented | bench/HIL control and diagnostics | forbidden | allowed by build flags |
| Bench ACK/TX Test | Kvaser/PCAN single-node TX counterpart | forbidden | ACK/TX allowed by explicit lab profile |

## Passive Product Data Flow

```text
Power/reset
  -> hardware-default safe pins
  -> CDC session open
  -> uplink/session quarantine cleanup
  -> quiet window
  -> CAN front-end initialization
  -> ACK-observe enable
  -> capability + lifecycle summaries
  -> new CAN_RX_SEGMENT evidence only
```

USB open/close may clean only CDC/uplink/session payload state. It must not
enable host TX/control or replay old payloads. In the passive field build,
MCP/built-in CAN initialization is held until CDC/DTR session stability plus
quiet time. A controlled transition to ACK-observe is allowed only after that
deferred initialization succeeds.

## Module Ownership Target

- `protocol/TypedFrame`: SOF, header, seq, length, CRC, endian helpers.
- `protocol/TypedRecords`: shared record IDs and payload layout.
- `CapabilityPublisher`: firmware profile, bus descriptors, passive claims.
- `CanRxLane_MCP2515`: bus0 MCP2515 deferred init and ACK-observe.
- `CanRxLane_Builtin`: bus1 Mid Carrier J4/U2 pre-session monitor and ACK-observe.
- `HostSessionRuntime`: CDC session epoch, DTR/session events, quarantine.
- `UplinkScheduler`: priority/descriptor/payload ownership, no silent clear.
- `HealthMonitor`: counters, violation latch, board health payload.
- `PassiveSupervisor`: mode readback, TXREQ zero verification, reset/power
  suspected latch.
- `ControlLane` / `CanTxAuditLane`: bench/full profile only.

`src/main.cpp` remains orchestration until the migration is finished, but new
behavior must be expressed through these ownership boundaries.

## Passive Invariants

- Passive Product compiles out host downlink, host TX, control TX, periodic test
  TX, and USB reconnect reset.
- bus0 MCP2515 is not initialized during USB power-up in passive field builds;
  it is initialized and moved to ACK-observe only after stable session plus quiet
  window.
- bus1 built-in CAN follows the same deferred-init rule.
- TXREQ bits must remain zero; violation latches and reports.
- Hardware evidence fields in CAPABILITY are claims/references, not proof.
- `passive_acceptance_allowed=true` requires nonzero hardware safety,
  bench verification, external analyzer artifact, and hotplug count.
- One-bus product artifacts are invalid; missing-bus mismatch diagnostics remain.
- Passive Product may ACK after session stability. Kvaser/PCAN TX tests require
  ACK-observe enabled; ACK remains separate from host TX/control.

## Wire Contract

Typed transport v1 is the production live output. Legacy/diagnostic binaries are
not field product paths. Any wire-format change must update:

- `include/protocol/TypedRecords.h`
- `shared/docs/TRANSPORT_AND_RECORDS_KO.md`
- matching VSM typed parser and capability decoder
