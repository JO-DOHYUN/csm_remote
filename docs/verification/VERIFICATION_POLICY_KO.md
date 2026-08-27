---
authority: PROCEDURE
owner: verification
status: active
read_when: selecting proof for a requested claim
---

# CSM Verification Policy

Static/source, native test, build, device, bench/HIL, vehicle and soak evidence are not
interchangeable. An unexecuted gate is NOT RUN. Criteria are fixed before execution.

| Level | Proof |
|---|---|
| H0 | harness route, metadata, context budget, links and stale/duplicate checks |
| C0 | L1/L2/L3 guards and deterministic native contracts |
| C1 | exact PlatformIO environment build and artifact identity |
| D1 | exact firmware upload/boot/device evidence |
| I1 | one transport/control route with fault injection and external analyzer where physical |
| I2 | simultaneous RC/control, dual CAN, USB, Wi-Fi and Android operation |
| R1 | fixed-criteria soak, recovery and field/release gates |

Required evidence separates authority/admission/terminal/physical facts, proves bounded loss and
recovery, and records exact source/environment/artifact/device identity. Physical timing/payload/ACK
claims require an external analyzer.

Failure method:
`symptom -> evidence -> root-cause boundary -> hypothesis -> minimal experiment -> fix -> verification`.
Repeated same-layer compensation triggers architecture review, not criterion relaxation.

Current qualification/release evidence is owned separately by
`qualification-status.yaml`; this stable policy does not cache a current gate.
