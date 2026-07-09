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
CSM_REMOTE_PRODUCT_DEFINITION_KO.md를 CSM Remote 프로젝트의 한국어 마스터 제품 정의서로 둔다.
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
CSM_REMOTE_PRODUCT_DEFINITION_KO.md
CSM_REMOTE_PRODUCT_DEFINITION.md
README.md
```

---

## ADR-0002: Development Harness Required

Date: 2026-07-09  
Status: Accepted  

Decision:

```text
제품 개발 전 CSM_REMOTE_DEVELOPMENT_HARNESS_AGENT_ARCHITECTURE_KO.md를 두고,
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
CSM_REMOTE_DEVELOPMENT_HARNESS_AGENT_ARCHITECTURE_KO.md
CSM_REMOTE_DECISION_LEDGER_KO.md
CSM_REMOTE_OPEN_DECISIONS_KO.md
CSM_REMOTE_CHANGE_HISTORY_KO.md
CSM_REMOTE_REQUIREMENT_TRACE_KO.md
```

---

## ADR-0003: Token-Efficient Agent Routing And Lean Completeness

Date: 2026-07-09  
Status: Accepted  

Decision:

```text
모든 작업은 CSM_REMOTE_AGENT_ROUTING_MATRIX_KO.md에서 시작한다.
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
CSM_REMOTE_AGENT_ROUTING_MATRIX_KO.md
CSM_REMOTE_DEVELOPMENT_HARNESS_AGENT_ARCHITECTURE_KO.md
CSM_REMOTE_REQUIREMENT_TRACE_KO.md
CSM_REMOTE_CHANGE_HISTORY_KO.md
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
CSM_REMOTE_REPOSITORY_SETUP_KO.md
CSM_REMOTE_CHANGE_HISTORY_KO.md
```
