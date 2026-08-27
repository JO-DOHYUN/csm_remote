---
name: can-hil
kind: procedure
description: Use for authorized physical CAN bench, external analyzer timing, fault injection, RC/Host coexistence, or long soak.
---

# CAN HIL

## Purpose
Prove physical behavior that source/build tests cannot establish.

## Inputs
Explicit hardware authorization, wiring/bitrate/channel, artifact identity, fixed criteria.

## Authority to Read
Git/artifact identity, Product Constitution, active manifest, verification policy, canonical wire,
exact HIL tool and fixed criteria.

## Procedure
1. Verify target hardware, transceiver power, wiring, termination, analyzer and safe motion state.
2. Freeze criteria before execution.
3. Capture raw analyzer truth and correlated typed evidence with timestamps/identity.
4. Test normal, stale/disconnect, authority transition, cancellation/failure, reset and load as applicable.
5. Preserve raw artifacts/hash and judge without modifying criteria.

## Stop / Escalation
No transmit/upload/vehicle action without explicit authorization. Abort on unknown target, unsafe motion,
bus identity mismatch, or unbounded fault injection.

## Required Verification
External analyzer plus correlated ACK/terminal/CAN_TX_RAW; soak only when actually completed.

## Output Contract
Bench topology, artifact identity, fixed criteria, measured results, verdict, unresolved field risk.
