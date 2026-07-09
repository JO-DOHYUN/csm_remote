# CSM Remote Open Decisions

작성일: 2026-07-09  
목적: 아직 추측으로 닫으면 안 되는 제품/구현 결정을 관리한다.

Open decision이 닫히기 전에는 관련 product vehicle TX를 허용하지 않는다.

---

## Status Values

```text
Open
EvidenceRequired
ReadyForDecision
ClosedAccepted
ClosedRejected
BlockedExternal
```

---

## Open Decision Template

```text
ID:
Title:
Status:
Owner:
Opened:
Decision Needed:
Why Open:
Required Evidence:
Allowed Temporary Assumption:
Blocked Work:
Linked ADR:
Linked Requirements:
Closure Criteria:
```

---

## OD-001: Final Upstream Autonomous Command Profile

Status: EvidenceRequired  
Decision Needed:

```text
AutonomyAuthorityMonitor가 어떤 CAN bus/ID/DLC/bit/counter/timing으로 upstream autonomous motion authority를 판정할지 확정한다.
```

Why Open:

```text
차량 upstream autonomous protocol이 아직 이 문서 기준으로 확정되지 않았다.
```

Required Evidence:

```text
실 차량 또는 HIL CAN capture
upstream command frame definition
release/active/timeout semantics
counter/checksum/mode bit definition
```

Blocked Work:

```text
실 차량 remote local TX
final AutonomyCommandProfile
```

---

## OD-002: D1 CanTxEnable Hardware Semantics

Status: EvidenceRequired  
Decision Needed:

```text
D1을 RX/ACK transceiver enable과 local motion TX enable 중 어떤 의미로 최종 정의할지 확정한다.
```

Why Open:

```text
현재 CSM은 passive ACK/RX transceiver enable 목적에서도 D1을 HIGH로 만들 수 있다.
따라서 D1 HIGH를 local control authorized로 해석하면 안 된다.
```

Required Evidence:

```text
현재 CSM 회로/결선 확인
D1 waveform/scope capture
passive ACK/RX 동작 확인
local TX gate 설계안
```

Blocked Work:

```text
product local TX gate finalization
vehicle remote TX
```

---

## OD-003: R16SM Actual CRSF Baud And Mode

Status: EvidenceRequired  
Decision Needed:

```text
우리 R16SM/T16D 설정에서 실제 CRSF baud, frame cadence, mode indication을 확정한다.
```

Required Evidence:

```text
logic analyzer capture
R16SM LED/mode 기록
Serial3 RX electrical capture
valid frame decode evidence
```

Blocked Work:

```text
final CRSF UART configuration
M4 parser timing final values
```

---

## OD-004: Final M4-M7 IPC Mechanism

Status: EvidenceRequired  
Decision Needed:

```text
Portenta H7에서 M4-M7 RcSample handoff를 seqlock/double-buffer/HSEM/OpenAMP/RPC 중 무엇으로 구현할지 확정한다.
```

Required Evidence:

```text
dual-core boot behavior
cache/coherency rules
memory barrier method
torn-read test
stale/heartbeat test
```

Current Phase 1D Result:

```text
64-byte M4RemoteMailboxFrame, seqlock begin/end rule, CRC16-CCITT, sample state mapping은 코드와 문서로 고정했다.
하지만 shared memory 위치, cache barrier, HSEM/OpenAMP/RPC 선택은 아직 확정하지 않았다.
```

Allowed Temporary Assumption:

```text
M7 code may compile and test the fixed frame decoder, but production M4-M7 IPC binding must remain disabled until closure evidence exists.
```

Blocked Work:

```text
production M4-M7 mailbox
RC source authority path
```

---

## OD-005: Real Vehicle CAN Command IDs And Payloads

Status: EvidenceRequired  
Decision Needed:

```text
VehicleCommandMapper가 생성할 실제 CAN ID, DLC, payload, counter, checksum, limits를 확정한다.
```

Required Evidence:

```text
vehicle CAN model/profile
HIL validation
neutral/failsafe frame definition
rate/limit definition
```

Blocked Work:

```text
실 차량 motion-control mapping
low-rate vehicle remote command
```

---

## OD-006: Remote Channel Map And Switch Semantics

Status: EvidenceRequired  
Decision Needed:

```text
T16D channel assignment, takeover switch, release switch, mode switch, neutral deadband를 확정한다.
```

Required Evidence:

```text
operator test
channel capture
failsafe behavior capture
neutral/debounce validation
```

Blocked Work:

```text
RemoteControlSource final takeover logic
```

---

## OD-007: Service/HIL Authority Policy

Status: Open  
Decision Needed:

```text
ServiceHilOnly profile에서 host/VSM control을 어느 수준까지 허용할지 정한다.
```

Allowed Temporary Assumption:

```text
product default에서는 disabled.
```

Blocked Work:

```text
service profile control features
```

---

## OD-008: Final Watchdog/Reset Behavior

Status: Open  
Decision Needed:

```text
M4 stale, M7 fault, estop, autonomy reappearance, mailbox corruption 시 reset/watchdog/fault-hold 정책을 확정한다.
```

Blocked Work:

```text
vehicle validation readiness
final fault recovery policy
```
