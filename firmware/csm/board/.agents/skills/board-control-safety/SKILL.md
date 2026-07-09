---
name: board-control-safety
description: Use when changing arm/lease/heartbeat/neutral/estop behavior, control record flow, or CAN TX audit chain.
---

# board-control-safety

## Goal
Control must be safe, explicit, and replay-auditable.

## Required State Model
- `MONITOR_ONLY`
- `CONTROL_STANDBY`
- `CONTROL_ARMED`
- `FAULT`
- `ESTOP`

## Mandatory Rules
- no auto-arm on reconnect
- no control output without valid lease and heartbeat
- timeout must force neutral or output-off policy
- estop must be latched until explicit reset policy is met
- actual CAN TX must be logged separately from requested command

## Procedure
1. inspect current state machine and command flow
2. keep audit chain explicit:
   - host requested intent
   - board accepted intent
   - actual CAN TX raw
   - control ack or event
3. document every new fault reason
4. confirm capture path remains independent from control path

## Acceptance
- heartbeat loss produces deterministic safe transition
- invalid command is rejected with visible reason
- accepted command and actual CAN TX can be compared later
- estop path cannot be bypassed by stale host packets
