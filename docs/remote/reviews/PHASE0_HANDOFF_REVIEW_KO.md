# CSM Remote Phase 0 Handoff Review

작성일: 2026-07-09
상태: Phase 0 판정 완료
대상 문서: `docs/remote/reviews/AUTHORITY_FINAL_HANDOFF_ORIGINAL.md`
상위 기준: `docs/remote/product/PRODUCT_DEFINITION_KO.md`
대상 코드: `firmware/csm` subtree, CSM baseline `bfef287`

이 문서는 기존 handoff를 현재 제품 정의서와 실제 CSM 코드 기준으로 재판정한다.

판정 값:

```text
ACCEPT      그대로 유지 가능
MODIFY      방향은 맞지만 현재 제품 정의/코드 사실에 맞게 수정 필요
HOLD        open decision/evidence가 닫히기 전 구현 금지
REJECT      현재 제품 방향에서 폐기
SUPERSEDED  제품 정의서가 더 정확한 문장으로 대체
```

---

## 1. 최종 결론

기존 handoff의 큰 방향은 유지한다.

```text
M4 = RC frontend only
M7 = sole authority owner
M7 = sole CAN TX owner
VSM = observer by default
No confirmed autonomy release = zero local CAN TX
```

하지만 handoff는 최종 구현 기준으로는 부족하다.

제품 정의서가 다음 항목을 더 정확하게 대체한다.

```text
D1 CanTxEnable 의미
Token-efficient 개발 라우팅
ScenarioFit gate
Final-Architecture-First 구현 방식
Residue Cleanup gate
Lean Completeness 원칙
git subtree 기반 firmware/csm 구조
```

Phase 1로 넘어가기 전 필수 결론:

```text
handoff는 배경 문서로 유지한다.
구현 기준은 Product Definition + Routing Matrix + Source Layout Proposal이다.
```

---

## 2. 실제 코드 기준 핵심 사실

### 2.1 CSM baseline

```text
firmware/csm
baseline bfef287aa424edcef6026dcd86aa6e4077a82386
passive build verified successfully
```

### 2.2 현재 존재하는 것

현재 CSM에 이미 존재하는 기반:

```text
BoardPins.h
SafetySupervisor
TypedFrame / TypedRecords
CapabilityPublisher
ControlPolicy
HostDownlinkParser
UplinkScheduler / SerialTxScheduler / CanRxSegmentBuilder
passive_guard.py
passive product PlatformIO env
```

### 2.3 현재 없는 것

현재 CSM에 없는 것:

```text
M4 build/env for remote frontend
CRSF parser
M4-M7 mailbox
RemoteControlSource
AutonomyAuthorityMonitor
AuthorityManager
CommandLimiter
VehicleCommandMapper
CanTxGateway
remote-specific evidence/status records
remote-specific build guards
```

### 2.4 D1 CanTxEnable 충돌

현재 코드 사실:

```text
firmware/csm/src/main.cpp update_safety_state()
rx_transceiver_enable || safety_supervisor.canDriveTxGate() || should_enable_can_tx_gate_for_test()
조건으로 BoardPins::CanTxEnable을 HIGH로 만들 수 있다.
```

passive ACK observe path에서도:

```text
ack_observe_enabled && BOARD_CAN_TRANSCEIVER_ENABLE_FOR_RX
조건으로 D1이 HIGH가 될 수 있다.
```

따라서 handoff의 다음 문장은 그대로 구현 기준으로 쓸 수 없다.

```text
When local control is not authorized:
  CanTxEnable = LOW
```

판정:

```text
MODIFY / HOLD
```

제품 기준:

```text
D1 HIGH는 local motion-control authority의 증거가 아니다.
local motion-control CAN TX는 CanTxGateway와 driver-level gate를 통과해야 한다.
D1 최종 의미는 OD-002가 닫힌 뒤 확정한다.
```

---

## 3. Handoff 섹션별 판정

### 3.1 Section 0, Non-negotiable project philosophy

판정:

```text
ACCEPT
```

유지할 내용:

```text
CSM은 upstream autonomous controller와 경쟁하지 않는다.
Unknown/Ambiguous/RecentlyActive/ProtocolFault는 local TX를 막는다.
RC는 M7 authority logic을 우회하지 않는다.
M4는 vehicle controller가 아니다.
VSM은 observer-only default다.
```

보강:

```text
작업 시작은 Routing Matrix를 거친다.
구상 변경은 ScenarioFit과 Residue Cleanup을 거친다.
```

### 3.2 Section 1, Hardware scope

판정:

```text
MODIFY
```

유지:

```text
Portenta H7 + Mid Carrier + T16D + R16SM
기존 D0-D14/A0/A1/A6/A7 pin 보존
RC는 UART3 / Serial3 / J14 RX3/TX3 사용
```

수정:

```text
CRSF UART mode는 1차 방향이지만 OD-003 실측 전 final로 닫지 않는다.
D1 CanTxEnable은 local authority gate로 단정하지 않는다.
전원/ground/UART level/back-powering/boot output은 bench evidence 필요.
```

### 3.3 Section 2, Core safety invariants

판정:

```text
ACCEPT with clarification
```

유지:

```text
AutonomyAuthorityState != InactiveConfirmed이면 local TX 0
Unknown is unsafe
handoff gates 필요
reappearance preempts local
single local CAN TX exit
RC does not know CAN
VSM observer-only default
no raw CAN downlink in product mode
stale/failsafe last value reuse 금지
evidence 분리
```

수정:

```text
CanTxEnable LOW 요구는 D1 semantic conflict 때문에 OD-002까지 보류한다.
하드웨어 gate 증명은 D1 단독이 아니라 CanTxGateState + driver-level prohibition까지 포함한다.
```

### 3.4 Section 3, Correct conceptual architecture

판정:

```text
ACCEPT
```

유지:

```text
M4 = CRSF receiver frontend
M7 = sole authority owner and sole CAN TX owner
VSM = observer by default
AutonomyAuthorityMonitor is above RemoteControlSource and HostServiceSource
```

구현 주의:

```text
현재 코드에는 이 모듈들이 없으므로 Phase 1에서 deny-first skeleton부터 만든다.
```

### 3.5 Section 4, M4 responsibility boundary

판정:

```text
ACCEPT
```

유지:

```text
M4는 UART bytes, CRSF frame, channel unpack, normalize, stale/failsafe, RcSample publish만 수행한다.
M4는 CAN ID/payload/TX/CanTxEnable/authority/SafetySupervisor를 모른다.
```

보강:

```text
M4 build/env와 source path는 Source Layout Proposal에서 별도 정의한다.
```

### 3.6 Section 5, M4 to M7 handoff design

판정:

```text
ACCEPT with stronger constraint
```

유지:

```text
latest-sample mailbox
FIFO queue 금지
magic/version/seq/time/channels/state/crc 검증
```

보강:

```text
naive shared struct 금지
seqlock/double-buffer/HSEM/OpenAMP/RPC 중 실제 Portenta dual-core 증거 기반 선택 필요
OD-004가 닫히기 전 product mailbox 확정 금지
```

### 3.7 Section 6, M7 module boundaries

판정:

```text
MODIFY
```

방향은 맞다.
다만 실제 CSM repo 구조에 맞춰 header/source 배치를 조정한다.

적용 구조:

```text
include/board/authority/
src/board/authority/
include/board/control/
src/board/control/
include/board/remote/
src/board/remote/
include/board/host/
src/board/host/
```

주의:

```text
main.cpp는 module orchestration만 수행한다.
parser/authority/mapper/gateway 본문을 main.cpp에 넣지 않는다.
```

### 3.8 Section 7, AutonomyAuthorityMonitor design

판정:

```text
ACCEPT / HOLD for real vehicle profile
```

유지:

```text
profile-driven monitor
Unknown, InactiveConfirmed, ActiveConfirmed, RecentlyActive, Ambiguous, ProtocolFault
Only InactiveConfirmed allows local control consideration
```

보류:

```text
실제 CAN ID/DLC/bit/counter/timing은 OD-001이 닫히기 전 확정 금지
```

Phase 1 허용:

```text
type/skeleton/state transition shell
deny-first default Unknown
```

### 3.9 Section 8, AuthorityManager design

판정:

```text
ACCEPT
```

유지:

```text
AuthorityManager owns local source ownership.
SafetySupervisor는 source-agnostic safety gate로 유지한다.
Autonomy가 release되지 않으면 Remote/VSM 우선순위는 의미가 없다.
```

구현 주의:

```text
SafetySupervisor에 RC-specific policy를 넣지 않는다.
AuthorityManager가 CAN write를 호출하지 않는다.
```

### 3.10 Section 9, OperatorCommand and CAN mapping

판정:

```text
ACCEPT / HOLD for real vehicle CAN mapping
```

유지:

```text
RemoteControlSource는 OperatorCommand만 만든다.
VehicleCommandMapper만 CAN frame candidate를 만든다.
```

보류:

```text
실제 vehicle CAN ID/payload/counter/checksum/rate는 OD-005 전 확정 금지
```

### 3.11 Section 10, Build profiles and guards

판정:

```text
MODIFY
```

이유:

```text
handoff의 flag 이름은 개념상 맞지만 현재 CSM의 실제 flag와 일부 다르다.
현재 CSM은 BOARD_ENABLE_HOST_DOWNLINK, BOARD_ENABLE_HOST_CAN_TX_* 계열을 사용한다.
BOARD_ENABLE_RAW_CAN_DOWNLINK는 아직 실제 코드 flag가 아니다.
```

현재 사실:

```text
passive env는 BOARD_CSM_PROFILE_PASSIVE_PRODUCT=1
BOARD_ENABLE_HOST_CAN_TX=0
BOARD_ENABLE_HOST_CAN_TX_BUILTIN=0
BOARD_ENABLE_HOST_CAN_TX_MCP2515=0
BOARD_ENABLE_HOST_DOWNLINK=0
BOARD_MCP2515_CONTROL_TX_ALLOWED=0
```

Phase 1 방향:

```text
remote-related flags는 기존 naming과 충돌하지 않게 추가한다.
remote-off/passive guard를 passive_guard.py 확장 또는 별도 remote_guard.py로 추가한다.
```

### 3.12 Section 11, Timing and embedded feasibility target

판정:

```text
ACCEPT as planning estimate / HOLD for final values
```

유지:

```text
bounded hot path
heap/string/JSON/unbounded queue 금지
M7 authority tick 100-200 Hz 후보
local CAN output 20 Hz 시작 후보
```

보류:

```text
R16SM baud/frame cadence는 OD-003 실측 전 확정 금지
```

### 3.13 Section 12, Required implementation order

판정:

```text
ACCEPT with Phase 0 inserted
```

적용:

```text
Phase 0: handoff review + source layout proposal
Phase 1: M7 architecture scaffolding, no CAN TX change
Phase 2: M4 CRSF read-only
Phase 3: autonomy observe/read-only
Phase 4: gateway/inhibit proof
Phase 5: low-rate remote candidate
```

### 3.14 Section 13, Acceptance tests

판정:

```text
ACCEPT
```

조치:

```text
Requirement Trace에 연결한다.
Phase별로 unit/static/bench/HIL로 나눈다.
```

### 3.15 Section 14, What Codex must not do

판정:

```text
ACCEPT
```

추가 금지:

```text
토큰/턴 제약으로 main.cpp에 임시 기능 몰아넣기
구상 변경 후 old residue 방치
필요 없는 agent/skill/document 반복 로딩
```

### 3.16 Section 15, Final definition of success

판정:

```text
ACCEPT / SUPERSEDED by Product Definition section 22
```

핵심 문장은 유지한다.

```text
No confirmed autonomy release = zero local CAN TX.
```

---

## 4. Handoff에서 폐기하거나 보류할 문장

### 4.1 폐기 없음

handoff의 핵심 철학 중 현재 제품 방향에서 완전히 폐기할 항목은 없다.

### 4.2 보류/수정 필수

다음은 그대로 구현하면 안 된다.

```text
CanTxEnable LOW whenever local control is not authorized
```

이유:

```text
현재 D1은 passive ACK/RX transceiver enable에도 사용될 수 있다.
```

대체 문장:

```text
D1 HIGH is not proof of local motion-control authority.
Local motion-control CAN TX requires CanTxGateway and driver-level permission.
```

다음도 보류한다.

```text
CRSF baud/timing final value
upstream autonomy CAN profile
vehicle CAN mapping
M4-M7 IPC primitive
remote channel/switch map
```

---

## 5. Phase 1 진입 조건

Phase 1은 다음 조건으로 시작 가능하다.

```text
1. CSM subtree baseline is fixed at firmware/csm.
2. Passive build is verified.
3. Handoff review is completed.
4. Source layout proposal is completed.
5. No real local CAN TX behavior will be added in Phase 1.
```

Phase 1에서 허용되는 코드:

```text
types
headers
deny-first skeletons
static helpers
compile-time flags off by default
unit/static guard scaffolding
```

Phase 1에서 금지되는 코드:

```text
new CAN TX behavior
real vehicle CAN mapping
M4 CRSF parser hot path
raw host downlink expansion
SafetySupervisor RC-specific policy
main.cpp bulk implementation
```

---

## 6. 최종 Phase 0 판정

```text
handoff direction: accepted
handoff as implementation authority: superseded
product definition: controlling
repository setup: complete
source layout proposal: required before code
Phase 1 code: allowed only as deny-first M7 skeleton
```

