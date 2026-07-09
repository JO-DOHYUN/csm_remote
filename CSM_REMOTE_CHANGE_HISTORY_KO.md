# CSM Remote Change History

작성일: 2026-07-09  
목적: 작업 흐름과 변경 이유를 짧게 기록한다.

---

## Entry Template

```text
Date:
Change ID:
Summary:
Reason:
Files Changed:
Affected Definitions:
Affected Decisions:
Tests/Evidence:
Residual Risk:
Next Step:
```

---

## 2026-07-07: Product Definition Created

Summary:

```text
CSM Remote의 한국어 마스터 제품 정의서와 영문 보조 정의서를 작성했다.
```

Files Changed:

```text
CSM_REMOTE_PRODUCT_DEFINITION_KO.md
CSM_REMOTE_PRODUCT_DEFINITION.md
README.md
```

Reason:

```text
CSM 리모컨 통합은 코드 기능 추가 전에 제품 철학, M4/M7 경계, CAN TX 권한, VSM 기본 권한, 상태/에러/evidence를 먼저 고정해야 한다.
```

Residual Risk:

```text
D1 gate semantics, upstream autonomy profile, R16SM actual baud, M4-M7 IPC, vehicle CAN mapping은 open decision으로 남음.
```

---

## 2026-07-09: Development Harness Architecture Created

Summary:

```text
개발 중 기능/구상 변경으로 인한 아키텍처 붕괴를 막기 위해 개발 하네스 에이전트 아키텍처를 작성했다.
```

Files Changed:

```text
CSM_REMOTE_DEVELOPMENT_HARNESS_AGENT_ARCHITECTURE_KO.md
README.md
CSM_REMOTE_DECISION_LEDGER_KO.md
CSM_REMOTE_OPEN_DECISIONS_KO.md
CSM_REMOTE_CHANGE_HISTORY_KO.md
CSM_REMOTE_REQUIREMENT_TRACE_KO.md
```

Reason:

```text
Architecture drift, boundary leakage, data flow scatter, shadow state machine, profile blur, evidence collapse를 개발 운영 차원에서 막아야 한다.
```

Next Step:

```text
CSM baseline import/copy strategy와 Phase 0 handoff 재판정으로 이동.
```

---

## 2026-07-09: Agent Routing And Lean Completeness Added

Summary:

```text
에이전트 하네스의 최종 목표를 완성도와 토큰 효율화로 명확히 하고,
작업 유형별 문서/섹션/에이전트 라우팅 매트릭스를 추가했다.
```

Files Changed:

```text
CSM_REMOTE_AGENT_ROUTING_MATRIX_KO.md
CSM_REMOTE_DEVELOPMENT_HARNESS_AGENT_ARCHITECTURE_KO.md
CSM_REMOTE_DECISION_LEDGER_KO.md
CSM_REMOTE_CHANGE_HISTORY_KO.md
CSM_REMOTE_REQUIREMENT_TRACE_KO.md
CSM_REMOTE_HARNESS_FINAL_AUDIT_KO.md
README.md
```

Reason:

```text
불필요한 agent/skill/document 로딩을 막고, 구상 변경 시 제품 실행-사용 시나리오 적합성,
최종 아키텍처 우선 구현, 잔재 정리, 간결한 완결성을 강제하기 위해서다.
```

Residual Risk:

```text
라우팅 매트릭스는 구현이 시작되면 실제 source layout과 test command에 맞춰 계속 갱신해야 한다.
```

Next Step:

```text
Phase 0에서 CSM baseline import/copy strategy와 handoff 재판정을 수행한다.
```

---

## 2026-07-09: CSM Imported As Git Subtree

Summary:

```text
`csm_remote`를 git repository로 초기화하고, 기존 CSM `bfef287`을 `firmware/csm` 아래에 git subtree로 통합했다.
```

Files Changed:

```text
.gitignore
.gitattributes
firmware/csm/
README.md
CSM_REMOTE_REPOSITORY_SETUP_KO.md
CSM_REMOTE_DECISION_LEDGER_KO.md
CSM_REMOTE_CHANGE_HISTORY_KO.md
```

Reason:

```text
복붙 없이 CSM baseline을 추적하고, 제품 정의/decision/trace와 firmware 변경을 같은 repo에서 관리하면서도,
나중에 `firmware/csm` prefix 기준으로 다시 분리할 수 있게 하기 위해서다.
```

Tests/Evidence:

```text
CSM upstream HEAD confirmed as bfef287aa424edcef6026dcd86aa6e4077a82386.
Subtree import commit created under firmware/csm.
PlatformIO passive env build succeeded from firmware/csm.
git subtree split --prefix=firmware/csm returned bfef287.
```

Next Step:

```text
Phase 0 handoff 재판정과 CSM source layout proposal 작성.
```

---

## 2026-07-09: Phase 0 Handoff Review And Source Layout Proposal

Summary:

```text
기존 handoff 문서를 Product Definition 기준으로 재판정하고,
현재 firmware/csm 구조에 맞는 Phase 1 source layout proposal을 작성했다.
```

Files Changed:

```text
CSM_REMOTE_PHASE0_HANDOFF_REVIEW_KO.md
CSM_REMOTE_SOURCE_LAYOUT_PROPOSAL_KO.md
README.md
CSM_REMOTE_CHANGE_HISTORY_KO.md
```

Reason:

```text
코드 구현 전 handoff의 유지/수정/보류/SUPERSEDED 항목을 명확히 하고,
main.cpp에 기능을 몰아넣지 않도록 최종 모듈 배치를 먼저 확정하기 위해서다.
```

Tests/Evidence:

```text
firmware/csm BoardPins.h, platformio.ini, main.cpp, SafetySupervisor, passive_guard.py를 근거로 판정했다.
PlatformIO passive env build succeeded after Phase 0 documentation changes.
```

Next Step:

```text
Phase 1A: AuthorityTypes / RemoteTypes / OperatorCommand deny-first type skeleton 작성.
```

---

## 2026-07-09: Phase 1A Deny-First Type Skeleton

Summary:

```text
Phase 1A로 M7 authority/control/remote 경계의 첫 타입 skeleton을 추가했다.
```

Files Changed:

```text
firmware/csm/include/board/authority/AuthorityTypes.h
firmware/csm/src/board/authority/AuthorityTypes.cpp
firmware/csm/include/board/remote/RemoteTypes.h
firmware/csm/src/board/remote/RemoteTypes.cpp
firmware/csm/include/board/control/OperatorCommand.h
firmware/csm/src/board/control/OperatorCommand.cpp
CSM_REMOTE_CHANGE_HISTORY_KO.md
CSM_REMOTE_REQUIREMENT_TRACE_KO.md
```

Reason:

```text
Phase 1B/C 구현 전에 AutonomyAuthorityState, AuthorityState, ControlSourceId,
ControlDecisionCode, RcSample, RemoteLinkState, OperatorCommand의 단일 타입 계약을 먼저 고정하기 위해서다.
```

Tests/Evidence:

```text
새 타입은 compile anchor cpp를 통해 passive build에서 실제 컴파일되도록 배치했다.
PlatformIO passive env build succeeded and compiled AuthorityTypes.cpp, RemoteTypes.cpp, OperatorCommand.cpp.
```

Residual Risk:

```text
아직 AuthorityManager, AutonomyAuthorityMonitor, RemoteControlSource, CanTxGateway 동작은 구현하지 않았다.
```

Next Step:

```text
Phase 1B: AutonomyAuthorityMonitor / AuthorityManager / M4RemoteMailboxReader / RemoteControlSource deny-first skeleton.
```

---

## 2026-07-09: Phase 1B M7 Deny-First Skeleton

Summary:

```text
AutonomyAuthorityMonitor, AuthorityManager, M4RemoteMailboxReader, RemoteControlSource의 deny-first skeleton을 추가했다.
```

Files Changed:

```text
firmware/csm/include/board/authority/AutonomyAuthorityMonitor.h
firmware/csm/src/board/authority/AutonomyAuthorityMonitor.cpp
firmware/csm/include/board/authority/AuthorityManager.h
firmware/csm/src/board/authority/AuthorityManager.cpp
firmware/csm/include/board/remote/M4RemoteMailboxReader.h
firmware/csm/src/board/remote/M4RemoteMailboxReader.cpp
firmware/csm/include/board/remote/RemoteControlSource.h
firmware/csm/src/board/remote/RemoteControlSource.cpp
CSM_REMOTE_CHANGE_HISTORY_KO.md
CSM_REMOTE_REQUIREMENT_TRACE_KO.md
```

Reason:

```text
Phase 1C gateway/control skeleton으로 가기 전에 autonomy state, source authority,
remote mailbox snapshot, remote command source의 경계를 코드 구조로 고정하기 위해서다.
```

Boundary:

```text
main.cpp 변경 없음.
새 CAN TX 없음.
Vehicle CAN mapping 없음.
M4 CRSF parser 없음.
RemoteControlSource는 CAN ID/payload를 알지 않는다.
```

Tests/Evidence:

```text
Boundary search found no CAN write, D1 gate, HostDownlink, or payload path in the new skeleton files.
PlatformIO passive env build succeeded and compiled AuthorityManager.cpp,
AutonomyAuthorityMonitor.cpp, M4RemoteMailboxReader.cpp, RemoteControlSource.cpp.
```

Residual Risk:

```text
실제 upstream autonomy profile, M4-M7 IPC, RC channel mapping, vehicle CAN mapping은 아직 open decision이다.
```

Next Step:

```text
Phase 1C: CommandLimiter / VehicleCommandMapper / CanTxGateway deny-first skeleton.
```
