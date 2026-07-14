# CSM Remote Decision Ledger

작성일: 2026-07-09
목적: 제품/아키텍처/개발 운영 결정의 이유와 폐기된 대안을 기록한다.

결정은 코드보다 오래 남아야 한다.
구현 중 판단이 바뀌면 이 문서에 기록한 뒤 다음 작업으로 넘어간다.

---

## ADR Template

```text
ADR ID:
Date:
Status: Proposed / Accepted / Rejected / Superseded / Blocked / NeedsEvidence
Decision:
Context:
Options Considered:
Chosen Reason:
Rejected Alternatives:
Consequences:
Affected Product Definition Sections:
Affected Modules:
Affected Tests:
Supersedes:
Superseded By:
Evidence:
```

---

## ADR-0001: Product Definition Master

Date: 2026-07-07
Status: Accepted

Decision:

```text
docs/remote/product/PRODUCT_DEFINITION_KO.md를 CSM Remote 프로젝트의 한국어 마스터 제품 정의서로 둔다.
충돌 시 이 문서가 handoff 문서보다 우선한다.
```

Context:

```text
CSM Remote는 단순 기능 추가가 아니라 M4/M7 권한, CAN TX, VSM 권한, 차량 안전 경계를 재정의하는 제품 설계다.
```

Consequences:

```text
이후 코드 변경은 제품 정의서의 M4 parses only, M7 decides only, CanTxGateway sends only,
VSM observes by default 원칙을 기준으로 검토한다.
```

Affected Documents:

```text
docs/remote/product/PRODUCT_DEFINITION_KO.md
docs/remote/product/PRODUCT_DEFINITION_EN.md
README.md
```

---

## ADR-0002: Development Harness Required

Date: 2026-07-09
Status: Accepted

Decision:

```text
제품 개발 전 docs/remote/harness/DEVELOPMENT_HARNESS_AGENT_ARCHITECTURE_KO.md를 두고,
변경 영향, 모듈 경계, 데이터 흐름, 상태 모델, build profile, evidence, history를 관리한다.
```

Context:

```text
개발 중 기능과 방향이 바뀌면 임시 구현, 우회 경로, 중복 상태, 데이터 흐름 분산이 생길 수 있다.
이를 방지하려면 제품 하네스보다 먼저 개발 운영 하네스가 필요하다.
```

Consequences:

```text
중요 변경은 코드 작업 전에 ChangeIntake, DefinitionKeeper, ArchitectureBoundary,
DataFlow, StateModel, EvidenceContract 관점으로 검토한다.
```

Affected Documents:

```text
docs/remote/harness/DEVELOPMENT_HARNESS_AGENT_ARCHITECTURE_KO.md
docs/remote/product/DECISION_LEDGER_KO.md
docs/remote/product/OPEN_DECISIONS_KO.md
docs/remote/product/CHANGE_HISTORY_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
```

---

## ADR-0003: Token-Efficient Agent Routing And Lean Completeness

Date: 2026-07-09
Status: Accepted

Decision:

```text
모든 작업은 docs/remote/harness/ROUTING_MATRIX_KO.md에서 시작한다.
필요한 문서/섹션/에이전트만 읽고, 구현은 Lean Completeness 원칙을 따른다.
```

Context:

```text
개발 중 구상과 방향이 바뀌면 불필요한 문서/스킬/에이전트가 읽혀 토큰이 낭비되고,
잘못된 과거 지침이 활성화되어 개발 방향이 흔들릴 수 있다.
또한 턴/토큰 제약 때문에 기능을 main.cpp에 우선 몰아넣는 방식은 최종 완성도를 깨뜨린다.
```

Decision Detail:

```text
1. 라우팅 매트릭스로 작업 유형을 분류한다.
2. 관련 문서와 섹션만 읽는다.
3. 기본 활성 에이전트는 최대 3개로 제한한다.
4. 구상안은 ScenarioFit Gate를 통과해야 한다.
5. 코드는 Final-Architecture-First 방식으로 설계한다.
6. 구상 변경 시 Residue Cleanup Gate를 수행한다.
7. 설계는 Lean Completeness, 즉 간결한 완결성을 기준으로 판단한다.
```

Rejected Alternatives:

```text
매 작업마다 모든 문서를 읽는 방식
에이전트를 많이 켜서 누락을 막는 방식
기능을 먼저 붙이고 나중에 모듈화하는 방식
임시 구현을 product path에 남기는 방식
```

Consequences:

```text
작업 시작 비용은 줄고, 필요한 문맥만 읽는다.
대신 작업 유형 분류와 라우팅을 건너뛰면 하네스 위반이다.
```

Affected Documents:

```text
README.md
docs/remote/harness/ROUTING_MATRIX_KO.md
docs/remote/harness/DEVELOPMENT_HARNESS_AGENT_ARCHITECTURE_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
docs/remote/product/CHANGE_HISTORY_KO.md
```

---

## ADR-0004: Import CSM As Git Subtree

Date: 2026-07-09
Status: Accepted

Decision:

```text
기존 CSM `bfef287`을 `firmware/csm` prefix의 git subtree로 통합한다.
```

Context:

```text
CSM은 앞으로 직접 수정될 제품 firmware다.
하지만 나중에 별도 repo로 다시 분리할 가능성도 남겨야 한다.
복붙은 history와 diff 추적을 깨고, submodule은 제품 정의/trace와 firmware 변경 commit 흐름을 갈라놓는다.
```

Chosen Reason:

```text
subtree는 csm_remote 안에서 firmware를 직접 수정할 수 있게 하면서도,
prefix 기준 split/pull이 가능하다.
```

Rejected Alternatives:

```text
copy-paste: baseline 추적과 재분리 가능성이 약함
submodule: CSM을 외부 dependency처럼 만들어 현재 제품 개발 흐름에 맞지 않음
```

Consequences:

```text
CSM firmware는 `firmware/csm` 아래에서 작업한다.
upstream CSM 변경은 git subtree pull로 가져온다.
나중에 분리할 때는 git subtree split을 사용한다.
```

Affected Documents:

```text
README.md
docs/remote/operations/REPOSITORY_SETUP_KO.md
docs/remote/product/CHANGE_HISTORY_KO.md
```

---

## ADR-0005: Codex-Native AGENTS Routing

Date: 2026-07-09
Status: Accepted

Decision:

```text
CSM Remote workspace의 모든 작업은 루트 AGENTS.md에서 시작하고,
docs/remote/AGENTS.md와 firmware/csm/AGENTS.md가 경로별 하위 라우팅을 담당한다.
기존 flat root remote 문서들은 docs/remote/** 아래 역할별 구조로 이동한다.
```

Context:

```text
루트 README와 flat CSM_REMOTE_* 문서만으로는 Codex 실행 경로에서 자동 주입성이 약하다.
또한 firmware/csm 내부의 imported CSM AGENTS/BRIEF는 standalone CSM 전제를 갖고 있어,
remote 제품 정의와 충돌하거나 오래된 지침이 먼저 읽힐 위험이 있었다.
```

Chosen Reason:

```text
Codex 계열 작업자는 AGENTS.md 계층을 가장 자연스럽게 읽는다.
따라서 제품 권한은 루트 AGENTS.md와 docs/remote/AGENTS.md에 두고,
firmware/csm 내부 문서는 scoped firmware reference로 낮추는 구조가 가장 안정적이다.
```

Rejected Alternatives:

```text
모든 MD 삭제: baseline/hardware/wire evidence 손실이 큼
README만 강화: Codex 자동 실행 경로로는 약함
flat root MD 유지: 문서가 많아질수록 라우팅과 토큰 효율이 나빠짐
firmware/csm AGENTS 삭제: CSM subtree 분리 가능성과 firmware-local 규칙 손실
```

Consequences:

```text
새 작업은 AGENTS.md -> README.md -> docs/remote/AGENTS.md 순서로 들어간다.
firmware/csm/** 작업은 remote 제품 권한을 유지한 채 firmware/csm/AGENTS.md를 추가로 읽는다.
imported CSM 문서는 삭제하지 않고 하위 reference로만 사용한다.
```

Affected Documents:

```text
AGENTS.md
README.md
docs/AGENTS.md
docs/remote/AGENTS.md
firmware/AGENTS.md
firmware/csm/AGENTS.md
firmware/csm/BRIEF.md
firmware/csm/board/AGENTS.md
firmware/csm/board/docs/AGENTS.md
firmware/csm/docs/AGENTS.md
firmware/csm/shared/docs/AGENTS.md
docs/remote/product/CHANGE_HISTORY_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
```

Evidence:

```text
git diff --check passed.
python firmware/csm/tools/remote_phase1_guard.py passed.
root CSM_REMOTE_*.md remaining count: 0.
active router legacy CSM_REMOTE_*.md references: 0.
```

---

## ADR-0006: M4 Serial3 Capture Probe Is Lab-Only

Date: 2026-07-09
Status: Accepted

Decision:

```text
`portenta_h7_m4_remote_serial3_capture_probe` env와
`firmware/csm/src/m4_remote_serial3_capture_probe.cpp`는 제품 runtime이 아니라
R16SM/Serial3 evidence 수집용 lab-only profile로 둔다.
```

Context:

```text
Phase 2A에서 M4 target build는 확인했지만, 실제 R16SM CRSF 신호는 아직 검증되지 않았다.
Serial3를 곧바로 제품 runtime으로 열면 baud/polarity/pin/frame cadence 미확정 상태가 authority path로 새는 문제가 생긴다.
```

Chosen Reason:

```text
프로브를 M4 전용 source filter에 격리하면 Serial3 compile binding과 parser path를 확인하면서도,
M7 passive product, authority, vehicle mapping, CAN TX 경로를 건드리지 않을 수 있다.
```

Rejected Alternatives:

```text
M7 main.cpp에 임시 Serial3 코드를 넣는 방식: 제품 runtime 오염과 setup/loop 충돌 위험이 큼
M4 product frontend flag를 바로 켜는 방식: OD-003/OD-004 미종결 상태에서 authority path가 열림
Host/VSM raw CAN 경로로 리모컨을 우회 전달하는 방식: product boundary 위반
```

Consequences:

```text
OD-003은 닫히지 않는다.
Phase 2B 다음 작업은 logic analyzer capture와 captured-byte replay evidence여야 한다.
M4-M7 shared memory/IPC는 OD-004 evidence가 생길 때까지 별도 단계로 유지한다.
```

Affected Documents:

```text
docs/remote/architecture/CRSF_FRONTEND_CONTRACT_KO.md
docs/remote/architecture/SOURCE_LAYOUT_PROPOSAL_KO.md
docs/remote/operations/PHASE2B_R16SM_SERIAL3_CAPTURE_RUNBOOK_KO.md
docs/remote/product/OPEN_DECISIONS_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
docs/remote/product/CHANGE_HISTORY_KO.md
```

Evidence:

```text
python firmware/csm/tools/remote_phase2b_guard.py passed.
PlatformIO M4 env `portenta_h7_m4_remote_serial3_capture_probe` build succeeded.
```
