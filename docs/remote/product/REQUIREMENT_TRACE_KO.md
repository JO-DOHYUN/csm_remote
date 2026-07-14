# CSM Remote Requirement Trace

작성일: 2026-07-09
목적: 제품 정의 요구사항을 코드, 테스트, evidence와 연결한다.

이 문서는 구현이 시작되면 계속 갱신한다.

---

## Trace Status

```text
Defined
Designed
Implemented
UnitTested
FuzzTested
BenchTested
HilTested
VehicleTested
Blocked
```

---

## Trace Template

```text
Requirement ID:
Source Section:
Statement:
Status:
Code Modules:
Tests:
Evidence:
Open Decisions:
Notes:
```

---

## Initial Product Requirements

| ID | Source | Requirement | Status | Open Decisions |
|---|---|---|---|---|
| R-PROD-001 | Product Definition 17.2 | autonomy release 없으면 local TX 0 | Defined | OD-001 |
| R-PROD-002 | Product Definition 17.2 | M4 CAN TX capability 없음 | Defined | OD-004 |
| R-PROD-003 | Product Definition 17.2 | 모든 local motion TX는 CanTxGateway 통과 | Defined | OD-002 |
| R-PROD-004 | Product Definition 17.2 | VSM observer-only default | Defined | OD-007 |
| R-PROD-005 | Product Definition 17.2 | raw CAN downlink product disabled | Defined | OD-007 |
| R-PROD-006 | Product Definition 17.2 | RC stale/failsafe에서 last command 재사용 금지 | Defined | OD-003, OD-006 |
| R-PROD-007 | Product Definition 17.2 | autonomy reappearance 후 다음 local TX 전 inhibit | Defined | OD-001 |
| R-PROD-008 | Product Definition 17.2 | D1 semantic conflict resolved before product TX | Defined | OD-002 |
| R-PROD-009 | Product Definition 17.2 | accepted command = ACK + TX evidence | Defined | OD-005 |
| R-PROD-010 | Product Definition 17.2 | rejected command = reason + no TX evidence | Defined | none |
| R-PROD-011 | Product Definition 17.2 | CRSF malformed/oversize reject | Defined | OD-003 |
| R-PROD-012 | Product Definition 17.2 | mailbox torn/stale/corrupt reject | Defined | OD-004 |
| R-PROD-013 | Product Definition 17.2 | neutral-before-takeover enforced | Defined | OD-006 |
| R-PROD-014 | Product Definition 17.2 | vehicle profile before real CAN mapping | Defined | OD-005 |
| R-PROD-015 | Product Definition 17.2 | service/HIL profile cannot be product | Defined | OD-007 |

---

## Initial Development Harness Requirements

| ID | Source | Requirement | Status | Open Decisions |
|---|---|---|---|---|
| R-DEV-001 | Routing Matrix | 모든 작업은 작업 유형별 필요한 문서/섹션/에이전트만 읽는다 | Defined | none |
| R-DEV-002 | Harness 11 | 기본 활성 에이전트는 3개 이하로 제한한다, 예외는 MergeGate/HIL/vehicle review | Defined | none |
| R-DEV-003 | Harness 12 | 구상안은 실제 제품 실행-사용 시나리오 적합성을 검토한다 | Defined | OD-001, OD-002, OD-003, OD-005, OD-006 |
| R-DEV-004 | Harness 13 | 코드는 최종 module layout/interface/dataflow skeleton을 먼저 만들고 내부를 채운다 | Defined | none |
| R-DEV-005 | Harness 13 | main.cpp에 parser/authority/mapper/gateway 본문을 몰아넣지 않는다 | Defined | none |
| R-DEV-006 | Harness 14 | 구상 변경 시 old code/path/flag/test/doc residue를 정리하거나 명시적으로 남긴다 | Defined | none |
| R-DEV-007 | Harness 15 | 설계는 Lean Completeness, 즉 간결한 완결성을 기준으로 판단한다 | Defined | none |
| R-DEV-008 | Harness 15 | 간결성을 이유로 필수 boundary/state/evidence/test를 생략하지 않는다 | Defined | none |
| R-DEV-009 | Repository Setup | CSM firmware 작업은 `firmware/csm` subtree 안에서 수행하고 복붙 갱신을 금지한다 | Defined | none |
| R-DEV-010 | Phase 0 Review | 기존 handoff는 Product Definition 기준으로 ACCEPT/MODIFY/HOLD/REJECT/SUPERSEDED 판정을 거친다 | Designed | none |
| R-DEV-011 | Source Layout Proposal | Phase 1 구현 전 최종 module layout과 dataflow owner를 문서화한다 | Designed | none |
| R-DEV-012 | AGENTS Routing | 모든 작업은 루트 `AGENTS.md`에서 시작하고, 경로별 `AGENTS.md`가 하위 라우팅을 담당한다 | Implemented | none |

---

## Phase 1A Implementation Trace

| ID | Source | Requirement | Status | Code Modules |
|---|---|---|---|---|
| R-DEV-004 | Harness 13 | 코드는 최종 module layout/interface/dataflow skeleton을 먼저 만들고 내부를 채운다 | Implemented | `firmware/csm/include/board/authority/AuthorityTypes.h`, `firmware/csm/include/board/remote/RemoteTypes.h`, `firmware/csm/include/board/control/OperatorCommand.h` |
| R-DEV-005 | Harness 13 | main.cpp에 parser/authority/mapper/gateway 본문을 몰아넣지 않는다 | Implemented | no `main.cpp` changes |
| R-DEV-007 | Harness 15 | 설계는 Lean Completeness, 즉 간결한 완결성을 기준으로 판단한다 | Implemented | type-only skeleton with compile anchors |
| R-PROD-001 | Product Definition 17.2 | autonomy release 없으면 local TX 0 | Designed | `AutonomyAuthorityState`, `AuthorityDecision` deny-first defaults |
| R-PROD-002 | Product Definition 17.2 | M4 CAN TX capability 없음 | Designed | remote types contain no CAN ID/payload |
| R-PROD-003 | Product Definition 17.2 | 모든 local motion TX는 CanTxGateway 통과 | Designed | `OperatorCommand` contains normalized intent only |
| R-PROD-006 | Product Definition 17.2 | RC stale/failsafe에서 last command 재사용 금지 | Designed | `RemoteLinkState`, `RcSampleState` unusable stale/failsafe helpers |
| R-PROD-013 | Product Definition 17.2 | neutral-before-takeover enforced | Designed | neutral default `OperatorCommand` |

---

## Phase 1B Implementation Trace

| ID | Source | Requirement | Status | Code Modules |
|---|---|---|---|---|
| R-DEV-004 | Harness 13 | 코드는 최종 module layout/interface/dataflow skeleton을 먼저 만들고 내부를 채운다 | Implemented | `AutonomyAuthorityMonitor`, `AuthorityManager`, `M4RemoteMailboxReader`, `RemoteControlSource` skeletons |
| R-DEV-005 | Harness 13 | main.cpp에 parser/authority/mapper/gateway 본문을 몰아넣지 않는다 | Implemented | no `main.cpp` changes |
| R-PROD-001 | Product Definition 17.2 | autonomy release 없으면 local TX 0 | Designed | `AutonomyAuthorityMonitor` defaults to `Unknown`; `AuthorityManager` rejects non-`InactiveConfirmed` |
| R-PROD-002 | Product Definition 17.2 | M4 CAN TX capability 없음 | Designed | `M4RemoteMailboxReader` consumes only `RcSample` |
| R-PROD-003 | Product Definition 17.2 | 모든 local motion TX는 CanTxGateway 통과 | Designed | Phase 1B contains no CAN TX path |
| R-PROD-006 | Product Definition 17.2 | RC stale/failsafe에서 last command 재사용 금지 | Designed | `M4RemoteMailboxReader` rejects stale/failsafe samples; `RemoteControlSource` clears command |
| R-PROD-007 | Product Definition 17.2 | autonomy reappearance 후 다음 local TX 전 inhibit | Designed | `AutonomyAuthorityMonitor::observeFrame(..., local_authority_active=true)` latches inhibit |
| R-PROD-013 | Product Definition 17.2 | neutral-before-takeover enforced | Designed | `RemoteControlSource` requires `neutral` before emitting remote `OperatorCommand` |

---

## Phase 1C Implementation Trace

| ID | Source | Requirement | Status | Code Modules |
|---|---|---|---|---|
| R-DEV-004 | Harness 13 | 코드는 최종 module layout/interface/dataflow skeleton을 먼저 만들고 내부를 채운다 | Implemented | `CommandLimiter`, `VehicleCommandMapper`, `CanTxGateway` skeletons |
| R-DEV-005 | Harness 13 | main.cpp에 parser/authority/mapper/gateway 본문을 몰아넣지 않는다 | Implemented | no `main.cpp` changes |
| R-DEV-007 | Harness 15 | 설계는 Lean Completeness, 즉 간결한 완결성을 기준으로 판단한다 | Implemented | gateway는 평가 결과만 반환하고 실제 송신/counter/evidence를 만들지 않음 |
| R-PROD-003 | Product Definition 17.2 | 모든 local motion TX는 CanTxGateway 통과 | Designed | `CanTxGateway::evaluate()` policy/build/authority/inhibit/safety/hardware/backend/frame checks |
| R-PROD-005 | Product Definition 17.2 | raw CAN downlink product disabled | Designed | Phase 1C adds no HostDownlink or raw TX path |
| R-PROD-009 | Product Definition 17.2 | accepted command = ACK + TX evidence | Designed | acceptance can be evaluated, but no TX evidence is emitted yet |
| R-PROD-010 | Product Definition 17.2 | rejected command = reason + no TX evidence | Designed | all new modules return explicit reject detail and perform no TX |
| R-PROD-014 | Product Definition 17.2 | vehicle profile before real CAN mapping | Designed | `VehicleCommandMapper` rejects configured commands with `NoVehicleMapping` until vehicle profile closure |

---

## Phase 1D Implementation Trace

| ID | Source | Requirement | Status | Code Modules |
|---|---|---|---|---|
| R-DEV-004 | Harness 13 | 코드는 최종 module layout/interface/dataflow skeleton을 먼저 만들고 내부를 채운다 | Implemented | `M4RemoteMailboxContract`, `M4RemoteMailboxReader::updateFromMailboxFrame()` |
| R-DEV-005 | Harness 13 | main.cpp에 parser/authority/mapper/gateway 본문을 몰아넣지 않는다 | Implemented | no `main.cpp` changes |
| R-DEV-008 | Harness 15 | 간결성을 이유로 필수 boundary/state/evidence/test를 생략하지 않는다 | Implemented | fixed frame size, seqlock rule, CRC, reject detail, passive build evidence |
| R-PROD-002 | Product Definition 17.2 | M4 CAN TX capability 없음 | Designed | M4 mailbox frame contains RC sample only, no CAN ID or payload |
| R-PROD-011 | Product Definition 17.2 | CRSF malformed/oversize reject | Designed | M4 can report `CrcBad`/`ProtocolFault`; M7 maps them to `Malformed`/`ProtocolFault` reject |
| R-PROD-012 | Product Definition 17.2 | mailbox torn/stale/corrupt reject | Implemented | decoder rejects no-frame, torn sequence, bad magic/version/size/state, CRC mismatch, stale timeout |
| R-PROD-006 | Product Definition 17.2 | RC stale/failsafe에서 last command 재사용 금지 | Designed | non-OK sample states map to non-usable `RemoteLinkState` and do not create `OperatorCommand` |

---

## Phase 1E Implementation Trace

| ID | Source | Requirement | Status | Code Modules |
|---|---|---|---|---|
| R-DEV-004 | Harness 13 | 코드는 최종 module layout/interface/dataflow skeleton을 먼저 만들고 내부를 채운다 | Implemented | `CrsfParser`, `RcNormalizer` |
| R-DEV-005 | Harness 13 | main.cpp에 parser/authority/mapper/gateway 본문을 몰아넣지 않는다 | Implemented | no `main.cpp` changes |
| R-DEV-008 | Harness 15 | 간결성을 이유로 필수 boundary/state/evidence/test를 생략하지 않는다 | Implemented | frame length bounds, CRC reject, packed channel decode, configured normalizer |
| R-PROD-002 | Product Definition 17.2 | M4 CAN TX capability 없음 | Designed | CRSF parser and normalizer contain no CAN ID/payload or TX path |
| R-PROD-006 | Product Definition 17.2 | RC stale/failsafe에서 last command 재사용 금지 | Designed | normalizer emits only current `RcSample`; no last-stick reuse path |
| R-PROD-011 | Product Definition 17.2 | CRSF malformed/oversize reject | Implemented | parser rejects invalid length and CRC mismatch; decoder rejects non-0x16 or bad packed payload length |
| R-PROD-013 | Product Definition 17.2 | neutral-before-takeover enforced | Designed | channel values are normalized only; takeover/neutral semantics remain in `RemoteControlSource`/OD-006 |

---

## Phase 1F Implementation Trace

| ID | Source | Requirement | Status | Code Modules |
|---|---|---|---|---|
| R-DEV-004 | Harness 13 | 코드는 최종 module layout/interface/dataflow skeleton을 먼저 만들고 내부를 채운다 | Implemented | `M4RemoteMailboxWriter`, shared `RemoteTypes` channel range |
| R-DEV-005 | Harness 13 | main.cpp에 parser/authority/mapper/gateway 본문을 몰아넣지 않는다 | Implemented | no `main.cpp` changes |
| R-DEV-008 | Harness 15 | 간결성을 이유로 필수 boundary/state/evidence/test를 생략하지 않는다 | Implemented | producer-side sample validation, CRC, sequence fill, passive build evidence |
| R-PROD-002 | Product Definition 17.2 | M4 CAN TX capability 없음 | Designed | writer packs only `RcSample` into `M4RemoteMailboxFrame`, no CAN ID/payload |
| R-PROD-012 | Product Definition 17.2 | mailbox torn/stale/corrupt reject | Designed | writer produces matching sequence/CRC contract; actual torn-read bench evidence still OD-004 |
| R-PROD-006 | Product Definition 17.2 | RC stale/failsafe에서 last command 재사용 금지 | Designed | writer publishes explicit `RcSampleState`; M7 reader/source reject non-OK states |

---

## Phase 1G Implementation Trace

| ID | Source | Requirement | Status | Code Modules |
|---|---|---|---|---|
| R-DEV-004 | Harness 13 | 코드는 최종 module layout/interface/dataflow skeleton을 먼저 만들고 내부를 채운다 | Implemented | `RemoteContractSelfTest` |
| R-DEV-008 | Harness 15 | 간결성을 이유로 필수 boundary/state/evidence/test를 생략하지 않는다 | Implemented | synthetic CRSF/parser/normalizer/mailbox roundtrip helper, passive build evidence |
| R-PROD-011 | Product Definition 17.2 | CRSF malformed/oversize reject | Designed | self-test includes corrupt CRSF CRC rejection helper |
| R-PROD-012 | Product Definition 17.2 | mailbox torn/stale/corrupt reject | Designed | self-test roundtrips writer/reader sequence and CRC contract; hardware torn-read evidence remains OD-004 |
| R-PROD-002 | Product Definition 17.2 | M4 CAN TX capability 없음 | Designed | self-test touches only remote sample contracts, no CAN path |

---

## Phase 1H Implementation Trace

| ID | Source | Requirement | Status | Code Modules |
|---|---|---|---|---|
| R-DEV-004 | Harness 13 | 코드는 최종 module layout/interface/dataflow skeleton을 먼저 만들고 내부를 채운다 | Implemented | `RemoteControlOrchestrator` |
| R-DEV-005 | Harness 13 | main.cpp에 parser/authority/mapper/gateway 본문을 몰아넣지 않는다 | Implemented | orchestration skeleton is outside `main.cpp`; no `main.cpp` changes |
| R-DEV-008 | Harness 15 | 간결성을 이유로 필수 boundary/state/evidence/test를 생략하지 않는다 | Implemented | stage-specific reject result, authority/limit/map/gateway sequence, passive build evidence |
| R-PROD-001 | Product Definition 17.2 | autonomy release 없으면 local TX 0 | Designed | orchestrator checks `AuthorityManager` before limiter/mapper/gateway |
| R-PROD-003 | Product Definition 17.2 | 모든 local motion TX는 CanTxGateway 통과 | Designed | mapped frames are evaluated by `CanTxGateway`; no backend write exists in orchestrator |
| R-PROD-010 | Product Definition 17.2 | rejected command = reason + no TX evidence | Designed | orchestrator returns stage/decision/detail and performs no TX on reject |
| R-PROD-014 | Product Definition 17.2 | vehicle profile before real CAN mapping | Designed | current mapper still rejects with no real vehicle mapping |

---

## Phase 1I Implementation Trace

| ID | Source | Requirement | Status | Code Modules |
|---|---|---|---|---|
| R-DEV-004 | Harness 13 | 코드는 최종 module layout/interface/dataflow skeleton을 먼저 만들고 내부를 채운다 | Implemented | Phase 1 skeleton audit ties all added modules to the final dataflow |
| R-DEV-005 | Harness 13 | main.cpp에 parser/authority/mapper/gateway 본문을 몰아넣지 않는다 | Implemented | `remote_phase1_guard.py` fails if `main.cpp` wires Phase 1 remote runtime |
| R-DEV-006 | Harness 14 | 구상 변경 시 old code/path/flag/test/doc residue를 정리하거나 명시적으로 남긴다 | Implemented | Phase 1 audit lists remaining open decisions and Phase 2 entry criteria |
| R-DEV-008 | Harness 15 | 간결성을 이유로 필수 boundary/state/evidence/test를 생략하지 않는다 | Implemented | guard checks no direct UART/IPC/CAN TX IO, no real vehicle mapping, no gateway backend write |
| R-PROD-002 | Product Definition 17.2 | M4 CAN TX capability 없음 | Designed | guard fails if remote files acquire CAN frame/gateway knowledge |
| R-PROD-003 | Product Definition 17.2 | 모든 local motion TX는 CanTxGateway 통과 | Designed | guard ensures CanTxGateway has no backend write during Phase 1 |
| R-PROD-014 | Product Definition 17.2 | vehicle profile before real CAN mapping | Designed | guard fails if VehicleCommandMapper starts mapping real frames in Phase 1 |

---

## Codex Routing Implementation Trace

| ID | Source | Requirement | Status | Code/Docs |
|---|---|---|---|---|
| R-DEV-001 | Routing Matrix | 모든 작업은 작업 유형별 필요한 문서/섹션/에이전트만 읽는다 | Implemented | `AGENTS.md`, `docs/remote/AGENTS.md`, `docs/remote/harness/ROUTING_MATRIX_KO.md` |
| R-DEV-006 | Harness 14 | 구상 변경 시 old code/path/flag/test/doc residue를 정리하거나 명시적으로 남긴다 | Implemented | root flat remote docs moved to `docs/remote/**`; imported CSM docs retained as scoped references |
| R-DEV-009 | Repository Setup | CSM firmware 작업은 `firmware/csm` subtree 안에서 수행하고 복붙 갱신을 금지한다 | Implemented | `firmware/AGENTS.md`, `firmware/csm/AGENTS.md`, `firmware/csm/BRIEF.md` |
| R-DEV-012 | AGENTS Routing | 모든 작업은 루트 `AGENTS.md`에서 시작하고, 경로별 `AGENTS.md`가 하위 라우팅을 담당한다 | Implemented | root/docs/firmware/CSM scoped `AGENTS.md` hierarchy |

Evidence:

```text
git diff --check passed.
python firmware/csm/tools/remote_phase1_guard.py passed.
root CSM_REMOTE_*.md remaining count: 0.
active router legacy CSM_REMOTE_*.md references: 0.
```

---

## Phase 2A M4 Build Proof Trace

| ID | Source | Requirement | Status | Code Modules |
|---|---|---|---|---|
| R-DEV-004 | Harness 13 | 코드는 최종 module layout/interface/dataflow skeleton을 먼저 만들고 내부를 채운다 | Implemented | `firmware/csm/src/m4_remote_frontend_build_proof.cpp`, `platformio.ini` M4 env |
| R-DEV-005 | Harness 13 | main.cpp에 parser/authority/mapper/gateway 본문을 몰아넣지 않는다 | Implemented | M4 proof uses separate source file; M7 `main.cpp` unchanged |
| R-DEV-008 | Harness 15 | 간결성을 이유로 필수 boundary/state/evidence/test를 생략하지 않는다 | Implemented | `remote_phase2a_guard.py`, M4 build, M7 passive build |
| R-PROD-002 | Product Definition 17.2 | M4 CAN TX capability 없음 | Designed | M4 proof compiles parser/normalizer/mailbox writer only; no CAN source, authority, mapper, or gateway |
| R-PROD-011 | Product Definition 17.2 | CRSF malformed/oversize reject | Designed | M4 build compiles `CrsfParser` on `portenta_h7_m4` |
| R-PROD-012 | Product Definition 17.2 | mailbox torn/stale/corrupt reject | Designed | M4 build compiles `M4RemoteMailboxContract` and `M4RemoteMailboxWriter`; real IPC evidence remains OD-004 |

Evidence:

```text
python firmware/csm/tools/remote_phase1_guard.py passed.
python firmware/csm/tools/remote_phase2a_guard.py passed.
PlatformIO M4 env `portenta_h7_m4_remote_frontend_build_proof` build succeeded.
PlatformIO M7 passive env `portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive` build succeeded after M4 source-filter isolation.
```

---

## Phase 2B M4 Serial3 Capture Probe Trace

| ID | Source | Requirement | Status | Code Modules |
|---|---|---|---|---|
| R-DEV-004 | Harness 13 | 코드는 최종 module layout/interface/dataflow skeleton을 먼저 만들고 내부를 채운다 | Implemented | `firmware/csm/src/m4_remote_serial3_capture_probe.cpp`, `platformio.ini` M4 env |
| R-DEV-005 | Harness 13 | main.cpp에 parser/authority/mapper/gateway 본문을 몰아넣지 않는다 | Implemented | M4 probe uses separate source file; M7 `main.cpp` unchanged |
| R-DEV-008 | Harness 15 | 간결성을 이유로 필수 boundary/state/evidence/test를 생략하지 않는다 | Implemented | `remote_phase2b_guard.py`, M4 Serial3 capture probe build |
| R-PROD-002 | Product Definition 17.2 | M4 CAN TX capability 없음 | Designed | M4 probe opens only `Serial3` RX path and local mailbox writer; no CAN source, authority, mapper, or gateway |
| R-PROD-011 | Product Definition 17.2 | CRSF malformed/oversize reject | Designed | Probe feeds received bytes through `CrsfParser` and tracks length/CRC rejects |
| R-PROD-012 | Product Definition 17.2 | mailbox torn/stale/corrupt reject | Designed | Probe writes only a local `M4RemoteMailboxFrame`; real M4-M7 IPC remains OD-004 |

Evidence:

```text
python firmware/csm/tools/remote_phase1_guard.py passed.
python firmware/csm/tools/remote_phase2a_guard.py passed.
python firmware/csm/tools/remote_phase2b_guard.py passed.
PlatformIO M4 env `portenta_h7_m4_remote_serial3_capture_probe` build succeeded.
```

Residual Risk:

```text
This is not R16SM hardware proof.
OD-003 remains open until logic-analyzer capture, physical pin evidence, and valid hardware CRSF decode evidence exist.
```

---

## Phase 2C Mailbox Reject Self-Test Expansion Trace

| ID | Source | Requirement | Status | Code Modules |
|---|---|---|---|---|
| R-DEV-008 | Harness 15 | 간결성을 이유로 필수 boundary/state/evidence/test를 생략하지 않는다 | Implemented | `RemoteContractSelfTest` reject cases |
| R-PROD-006 | Product Definition 17.2 | RC stale/failsafe에서 last command 재사용 금지 | Designed | self-test checks stale timeout and failsafe rejection |
| R-PROD-012 | Product Definition 17.2 | mailbox torn/stale/corrupt reject | Designed | self-test checks torn-write, stale, and corrupt CRSF CRC rejection |
| R-PROD-002 | Product Definition 17.2 | M4 CAN TX capability 없음 | Designed | self-test remains remote contract only; no CAN source, mapper, gateway, or TX backend |

Evidence:

```text
python firmware/csm/tools/remote_phase1_guard.py passed.
python firmware/csm/tools/remote_phase2a_guard.py passed.
python firmware/csm/tools/remote_phase2b_guard.py passed.
PlatformIO M7 passive env `portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive` build succeeded.
```

Residual Risk:

```text
This is software contract evidence only.
OD-004 remains open until real M4-M7 shared memory placement, cache/barrier mechanism,
IPC selection, and hardware torn-read/stale bench evidence exist.
```
