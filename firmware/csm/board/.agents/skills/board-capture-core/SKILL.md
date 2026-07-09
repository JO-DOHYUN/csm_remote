---
name: board-capture-core
description: Use when changing timestamping, CAN RX capture, encoder direct capture, ADC sampling, typed records, or uplink prioritization.
---

# board-capture-core

## When to use
- CAN RX raw path
- encoder edge capture
- ADC/direct voltage capture
- mono64 timebase
- record schema
- uplink scheduling

## Inputs
- baseline files
- target lane(s)
- acceptance checks

## Outputs
- changed source files
- changed shared record docs if schema changed
- explicit risk notes

## Rules
- raw must stay raw
- derived values never overwrite raw
- no hidden drop
- no fake CAN frame for board-only sensor data
- telemetry priority must stay above debug burst data
- Passive Product host-absent mode is drain-and-discard: do not stage CAN
  payload while CDC/DTR host session is absent.
- Session-open quarantine clears CDC/uplink/session payload only; it must not
  stop CAN front-end drain or alter CAN controller/transceiver mode.
- Capability hardware evidence fields are claims/references, not verified proof.

## Procedure
1. read `board/AGENTS.md`, `board/BRIEF.md`, `shared/docs/TRANSPORT_AND_RECORDS_KO.md`
2. identify whether the change touches capture truth, record format, or scheduler policy
3. keep lane separation clear:
   - `CanRxLane`
   - `EncoderEdgeLane`
   - `AnalogSampleLane`
   - `ControlLane`
4. if schema changes, update shared docs in the same turn
5. if timing semantics change, state exact effect on replay and evidence logic
6. verify counters/events for overflow, timeout, resync, or malformed input
7. for passive reconnect changes, verify host-session epoch, transport epoch,
   host-absent summary, and pre-session payload replay counter

## Acceptance Examples
- same physical event keeps stable ordering on mono64 axis
- overflow increments visible counter and emits event
- analog burst cannot silently starve CAN RX export
- encoder raw and derived values are distinguishable in logs
- host-absent CAN frames are counted as discard and are not replayed after USB reconnect
