# CSM Verification Policy

Authority: `VERIFICATION PHILOSOPHY / CURRENT RELEASE GATE`

## Claim Rule

static/source, native test, build, device, bench/HIL, vehicle, soak를 서로 대체하지 않는다.
실행하지 않은 gate는 `NOT RUN`이며 PASS가 아니다. 시험 기준이나 threshold를 결과에
맞춰 바꾸지 않는다.

## Levels

| Level | Proof |
|---|---|
| H0 | harness routes, authority, links, duplicate/stale checks |
| C0 | Constitution/L2/experiment guards and deterministic native contracts |
| C1 | exact PlatformIO environment build and artifact identity |
| D1 | exact firmware upload/boot/device evidence |
| I1 | one transport/control route with fault injection and external analyzer where physical |
| I2 | RC/control + dual CAN load + USB + Wi-Fi/Android simultaneous operation |
| R1 | fixed-criteria long soak, recovery and field/release gates |

## Current Gate

Active manifest status is exploratory. Timing and transient values must follow:

```text
exploratory measurement
  -> reviewed value decision
  -> product constant freeze
  -> qualification HIL
```

Qualification is blocked until constants are frozen. The current source-derived enabled steady stream
is `1,095 records/s` and `131,513 B/s`; it is a sizing input, not throughput/HIL PASS.
Former `120 kB/s` and `135 kB/s` claims are HISTORY and not active acceptance thresholds.

## Required Evidence

- authority/admission/terminal/physical evidence correlation
- no silent drop, replay, duplicate catch-up or unbounded growth
- explicit cancellation vs hardware/tracking failure
- sink isolation and conservation under blocked/disconnected clients
- external analyzer for physical CAN timing/payload/ACK claims
- exact source/env/artifact/device identity

## Failure Method

`symptom -> evidence -> root-cause boundary -> hypothesis -> minimal experiment -> fix -> verification`.
Repeated same-layer compensation triggers architecture review rather than criterion relaxation.
