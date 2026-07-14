# CSM Remote Agent Routing Matrix

작성일: 2026-07-09
목적: 작업 상황별로 필요한 문서, 섹션, 에이전트만 읽게 하여 완성도와 토큰 효율을 동시에 유지한다.

이 문서는 매 작업의 짧은 진입점이다.
모든 문서를 매번 읽지 않는다.

---

## 1. 기본 사용법

```text
1. 사용자 요청을 작업 유형으로 분류한다.
2. 아래 matrix에서 필요한 문서/섹션만 읽는다.
3. 지정된 에이전트만 활성화한다.
4. 작업 중 영향 범위가 커지면 다시 matrix로 돌아온다.
5. 완료 전 MergeGate checklist를 적용한다.
```

금지:

```text
항상 전체 Product Definition 읽기
항상 전체 Handoff 읽기
모든 agent를 동시에 활성화하기
현재 작업과 무관한 skill/reference를 로드하기
```

---

## 2. 작업 유형별 라우팅

| 작업 유형 | 읽을 문서/섹션 | 활성 에이전트 | 읽지 말 것 |
|---|---|---|---|
| 단순 질문/방향 확인 | README, Routing Matrix, 관련 Product Definition 섹션 | ChangeIntake | 전체 CSM source, 전체 외부 reference |
| 새 구상안 토론 | Product Definition 1,2,4,5,19,20, Open Decisions | ChangeIntake, ScenarioFit, DefinitionKeeper | 구현 파일 전체 |
| 제품 정체성/목적 변경 | Product Definition 전체, Decision Ledger, Open Decisions | DefinitionKeeper, HistoryLedger, RedTeam | 코드 구현부터 시작 |
| M4 RC frontend 설계 | Product Definition 6.3,7.1,7.2,8.1,9.3,13,17, OD-003, OD-004 | ArchitectureBoundary, DataFlow, StateModel | CAN mapper/gateway 구현 세부 |
| M4 RC parser 구현 | 위 M4 섹션 + 해당 M4 source/test | ArchitectureBoundary, VerificationPlanner | VSM UI, vehicle CAN mapping |
| M4-M7 mailbox 설계 | Product Definition 7.2,8.1,9.3,17, OD-004 | DataFlow, StateModel, RedTeam | CRSF 외부 문서 반복 검색 |
| M7 authority 설계 | Product Definition 1,2,7.3,7.4,9.2,9.4,14,15, OD-001 | StateModel, DataFlow, DefinitionKeeper | M4 parser 구현 세부 |
| CanTxGateway 설계/구현 | Product Definition 1,2,6.5,7.10,8.3,10,11,12,17, OD-001, OD-002, OD-005 | ArchitectureBoundary, BuildProfile, EvidenceContract, RedTeam | VSM UI 세부 |
| VehicleCommandMapper 설계 | Product Definition 7.9,8.2,8.3,16,17, OD-005 | ArchitectureBoundary, DataFlow, VerificationPlanner | RC protocol parsing 세부 |
| VSM 관련 변경 | Product Definition 3.2,5.3,7.7,11,12, OD-007 | BuildProfile, EvidenceContract | M4 parser 세부 |
| Build profile 변경 | Product Definition 11,17, Open Decisions | BuildProfile, ArchitectureBoundary, VerificationPlanner | 외부 hardware reference |
| Evidence/record 변경 | Product Definition 12,17, Requirement Trace | EvidenceContract, VerificationPlanner | CRSF 구현 세부 |
| HIL/bench 계획 | Product Definition 17, Open Decisions, Requirement Trace | VerificationPlanner, EvidenceContract | code refactor 논쟁 |
| 구상안 변경 후 정리 | Harness 14, Decision Ledger, Change History, Open Decisions | HistoryLedger, ArchitectureBoundary, MergeGate | 새 기능 추가 |
| merge/마무리 판단 | Harness 16, Requirement Trace, Change History | MergeGate, EvidenceContract, HistoryLedger | 새로운 설계 토론 |

---

## 3. 상황별 필수 질문

### 3.1 구상안 토론

```text
실제 제품 실행-사용 시나리오와 맞는가?
현재 하드웨어/코드 사실로 가능한가?
open decision을 추측으로 닫지 않는가?
모듈 경계를 바꾸는가?
데이터 흐름이 하나로 유지되는가?
```

### 3.2 코드 설계

```text
최종 module layout이 먼저 있는가?
interface/type이 먼저 정의되었는가?
dataflow와 state owner가 명확한가?
main.cpp에 기능을 임시로 몰아넣지 않는가?
deny-first skeleton에서 시작하는가?
```

### 3.3 구상 변경

```text
superseded ADR이 기록되었는가?
old code path/build flag/test/doc이 제거 또는 명시적으로 남겨졌는가?
old handoff 문장이 새 정의와 충돌하지 않는가?
Requirement Trace가 갱신되었는가?
```

### 3.4 최적화/간결성 판단

```text
이 구조가 목표를 만족하는 가장 단순한 완성형인가?
필수 경계/상태/evidence/test를 생략하지 않았는가?
새 abstraction이 실제 복잡도를 줄이는가?
다음 작업자가 적은 문맥으로 이해할 수 있는가?
```

---

## 4. Escalation Rule

작업 중 다음이 발견되면 즉시 라우팅을 재분류한다.

```text
M4/M7 책임 변경
CAN TX 경로 변경
VSM 제어 권한 변경
build profile 의미 변경
D1/핀맵 의미 변경
AutonomyAuthorityState 변경
Evidence 의미 변경
vehicle CAN mapping 추가
open decision closure 필요
```

이 경우 코드 구현을 계속하지 말고 다음을 먼저 수행한다.

```text
Change Impact Note
ScenarioFit review
Decision Ledger update
Open Decision update
Requirement Trace update
```

