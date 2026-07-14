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
docs/remote/product/PRODUCT_DEFINITION_KO.md
docs/remote/product/PRODUCT_DEFINITION_EN.md
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
docs/remote/harness/DEVELOPMENT_HARNESS_AGENT_ARCHITECTURE_KO.md
README.md
docs/remote/product/DECISION_LEDGER_KO.md
docs/remote/product/OPEN_DECISIONS_KO.md
docs/remote/product/CHANGE_HISTORY_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
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
docs/remote/harness/ROUTING_MATRIX_KO.md
docs/remote/harness/DEVELOPMENT_HARNESS_AGENT_ARCHITECTURE_KO.md
docs/remote/product/DECISION_LEDGER_KO.md
docs/remote/product/CHANGE_HISTORY_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
docs/remote/harness/HARNESS_FINAL_AUDIT_KO.md
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
docs/remote/operations/REPOSITORY_SETUP_KO.md
docs/remote/product/DECISION_LEDGER_KO.md
docs/remote/product/CHANGE_HISTORY_KO.md
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
docs/remote/reviews/PHASE0_HANDOFF_REVIEW_KO.md
docs/remote/architecture/SOURCE_LAYOUT_PROPOSAL_KO.md
README.md
docs/remote/product/CHANGE_HISTORY_KO.md
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
docs/remote/product/CHANGE_HISTORY_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
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
docs/remote/product/CHANGE_HISTORY_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
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

---

## 2026-07-09: Phase 1C Control Gateway Deny-First Skeleton

Summary:

```text
CommandLimiter, VehicleCommandMapper, CanTxGateway의 deny-first skeleton을 추가했다.
```

Files Changed:

```text
firmware/csm/include/board/control/CommandLimiter.h
firmware/csm/src/board/control/CommandLimiter.cpp
firmware/csm/include/board/control/VehicleCommandMapper.h
firmware/csm/src/board/control/VehicleCommandMapper.cpp
firmware/csm/include/board/control/CanTxGateway.h
firmware/csm/src/board/control/CanTxGateway.cpp
docs/remote/product/CHANGE_HISTORY_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
```

Reason:

```text
리모컨/host source가 authority를 통과한 뒤에도 range limit, vehicle mapping,
CAN TX policy gate를 별도 모듈로 통과하도록 최종 데이터 흐름의 남은 경계를 고정하기 위해서다.
```

Boundary:

```text
main.cpp 변경 없음.
platformio.ini 변경 없음.
새 CAN TX backend write 없음.
VehicleCommandMapper는 실제 차량 CAN frame을 만들지 않고 명시적으로 reject한다.
CanTxGateway는 frame request를 평가만 하고 송신하지 않는다.
```

Tests/Evidence:

```text
Boundary search found no CAN write, D1 gate, HostDownlink, Serial3, MCP2515, or digitalWrite path in the new control/authority/remote skeleton files.
PlatformIO passive env build succeeded and compiled CanTxGateway.cpp, CommandLimiter.cpp, VehicleCommandMapper.cpp.
```

Residual Risk:

```text
실제 vehicle CAN profile, accepted command evidence, D1/local TX gate semantics,
CAN TX backend binding은 아직 open decision 이후 단계다.
```

Next Step:

```text
Phase 1D: M4-M7 mailbox/RC parser integration contract 또는 open decision closure 우선순위 확정.
```

---

## 2026-07-09: Phase 1D M4-M7 Remote Mailbox Contract

Summary:

```text
M4가 생산하고 M7이 소비할 64-byte RC mailbox frame contract와 decode skeleton을 추가했다.
```

Files Changed:

```text
firmware/csm/include/board/remote/M4RemoteMailboxContract.h
firmware/csm/src/board/remote/M4RemoteMailboxContract.cpp
firmware/csm/include/board/remote/M4RemoteMailboxReader.h
firmware/csm/src/board/remote/M4RemoteMailboxReader.cpp
firmware/csm/include/board/remote/RemoteTypes.h
firmware/csm/src/board/remote/RemoteTypes.cpp
docs/remote/architecture/M4_M7_REMOTE_MAILBOX_CONTRACT_KO.md
docs/remote/architecture/SOURCE_LAYOUT_PROPOSAL_KO.md
docs/remote/product/OPEN_DECISIONS_KO.md
docs/remote/product/CHANGE_HISTORY_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
README.md
```

Reason:

```text
M4 parser 구현 전 M7이 신뢰할 수 있는 handoff 경계를 먼저 고정하기 위해서다.
이제 M7은 외부 integrity_ok 플래그만 믿지 않고 seqlock, magic/version/size, sample state, CRC를 직접 검증할 수 있다.
```

Boundary:

```text
main.cpp 변경 없음.
platformio.ini 변경 없음.
M4 build env 없음.
실제 M4-M7 IPC/shared memory binding 없음.
CRSF parser 없음.
새 CAN TX 없음.
```

Tests/Evidence:

```text
M4RemoteMailboxFrame static_assert fixes the wire size at 64 bytes.
Boundary search found no CAN TX, D1 gate, HostDownlink, Serial3, MCP2515, or digitalWrite path in the new remote contract files.
PlatformIO passive env build succeeded and compiled M4RemoteMailboxContract.cpp.
```

Residual Risk:

```text
OD-004는 여전히 open이다. 실제 shared memory 위치, cache coherency, memory barrier,
HSEM/OpenAMP/RPC 선택, torn-read bench test는 아직 필요하다.
```

Next Step:

```text
Phase 1E: M4 CRSF parser/normalizer skeleton 또는 M7 unit guard/harness를 추가하기 전,
OD-003/OD-004 evidence 수집 계획을 확정한다.
```

---

## 2026-07-09: Phase 1E CRSF Parser And RC Normalizer Skeleton

Summary:

```text
CRSF byte/frame parser와 RC channel normalizer의 platform-independent skeleton을 추가했다.
```

Files Changed:

```text
firmware/csm/include/board/remote/CrsfParser.h
firmware/csm/src/board/remote/CrsfParser.cpp
firmware/csm/include/board/remote/RcNormalizer.h
firmware/csm/src/board/remote/RcNormalizer.cpp
docs/remote/architecture/CRSF_FRONTEND_CONTRACT_KO.md
docs/remote/architecture/SOURCE_LAYOUT_PROPOSAL_KO.md
docs/remote/product/OPEN_DECISIONS_KO.md
docs/remote/product/CHANGE_HISTORY_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
README.md
```

Reason:

```text
M4 UART/IPC 구현 전에도 CRSF frame bound, length/CRC reject, 0x16 RC_CHANNELS_PACKED unpack,
raw channel normalization 경계를 코드로 고정하기 위해서다.
```

Boundary:

```text
main.cpp 변경 없음.
platformio.ini 변경 없음.
M4 build env 없음.
UART/Serial3 binding 없음.
M4-M7 IPC/shared memory binding 없음.
takeover/release channel semantics 없음.
새 CAN TX 없음.
```

Tests/Evidence:

```text
Boundary search found no CAN TX, D1 gate, HostDownlink, Serial3, MCP2515, digitalWrite, HardwareSerial, or UART path in the new remote parser files.
PlatformIO passive env build succeeded and compiled CrsfParser.cpp and RcNormalizer.cpp.
```

Residual Risk:

```text
OD-003과 OD-006은 여전히 open이다. R16SM 실제 baud/mode/cadence와
T16D channel/switch assignment는 logic analyzer/operator evidence가 필요하다.
```

Next Step:

```text
Phase 1F: M4 mailbox writer helper 또는 parser/normalizer unit guard를 추가하되,
real UART/IPC/product TX는 계속 disabled로 유지한다.
```

---

## 2026-07-09: Phase 1F M4 Mailbox Writer Helper

Summary:

```text
RcSample을 64-byte M4RemoteMailboxFrame으로 포장하는 producer-side writer helper를 추가했다.
```

Files Changed:

```text
firmware/csm/include/board/remote/M4RemoteMailboxWriter.h
firmware/csm/src/board/remote/M4RemoteMailboxWriter.cpp
firmware/csm/include/board/remote/RemoteTypes.h
firmware/csm/src/board/remote/RcNormalizer.cpp
docs/remote/architecture/M4_M7_REMOTE_MAILBOX_CONTRACT_KO.md
docs/remote/architecture/SOURCE_LAYOUT_PROPOSAL_KO.md
docs/remote/product/OPEN_DECISIONS_KO.md
docs/remote/product/CHANGE_HISTORY_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
```

Reason:

```text
M4 parser/normalizer 출력이 M7 mailbox reader contract와 정확히 맞도록
producer-side pack/CRC/sequence 경계를 코드로 고정하기 위해서다.
```

Boundary:

```text
main.cpp 변경 없음.
platformio.ini 변경 없음.
M4 build env 없음.
UART/Serial3 binding 없음.
actual shared memory address 없음.
cache barrier/HSEM/OpenAMP/RPC binding 없음.
새 CAN TX 없음.
```

Tests/Evidence:

```text
Boundary search found no CAN TX, D1 gate, HostDownlink, Serial3, MCP2515, digitalWrite, HardwareSerial, UART, HSEM, OpenAMP, or RPC path in the remote files.
PlatformIO passive env build succeeded and compiled M4RemoteMailboxWriter.cpp.
```

Residual Risk:

```text
OD-004는 여전히 open이다. 실제 dual-core memory placement, barrier, coherency,
torn-read bench test가 닫히기 전까지 production IPC binding은 금지된다.
```

Next Step:

```text
Phase 1G: parser/normalizer/mailbox contract self-test harness 또는 M7 orchestration candidate skeleton.
```

---

## 2026-07-09: Phase 1G Remote Contract Self-Test Helper

Summary:

```text
synthetic CRSF frame부터 M7 mailbox snapshot까지 왕복하는 remote contract self-test helper를 추가했다.
```

Files Changed:

```text
firmware/csm/include/board/remote/RemoteContractSelfTest.h
firmware/csm/src/board/remote/RemoteContractSelfTest.cpp
docs/remote/architecture/CRSF_FRONTEND_CONTRACT_KO.md
docs/remote/architecture/M4_M7_REMOTE_MAILBOX_CONTRACT_KO.md
docs/remote/architecture/SOURCE_LAYOUT_PROPOSAL_KO.md
docs/remote/product/OPEN_DECISIONS_KO.md
docs/remote/product/CHANGE_HISTORY_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
```

Reason:

```text
CRSF parser, RC normalizer, M4 mailbox writer, M7 mailbox reader가 같은 contract로 맞물리는지
합성 입력 기준으로 확인할 수 있는 helper를 만들어 이후 UART/IPC 통합 전 regression 기준을 세우기 위해서다.
```

Boundary:

```text
main.cpp 변경 없음.
platformio.ini 변경 없음.
M4 build env 없음.
UART/Serial3 binding 없음.
actual shared memory address 없음.
cache barrier/HSEM/OpenAMP/RPC binding 없음.
새 CAN TX 없음.
self-test는 product runtime에 자동 연결되지 않음.
```

Tests/Evidence:

```text
Boundary search found no CAN TX, D1 gate, HostDownlink, Serial3, MCP2515, digitalWrite, HardwareSerial, UART, HSEM, OpenAMP, or RPC path in remote files.
PlatformIO passive env build succeeded and compiled RemoteContractSelfTest.cpp.
```

Residual Risk:

```text
이 self-test는 compile-ready synthetic helper다. 실제 실행형 unit test, R16SM capture,
dual-core torn-read bench evidence는 아직 별도 단계가 필요하다.
```

Next Step:

```text
Phase 1H: M7 remote orchestration candidate skeleton 또는 executable host/unit test harness.
```

---

## 2026-07-09: Phase 1H M7 Remote Control Orchestrator Skeleton

Summary:

```text
M7 remote control 한 tick의 제어 흐름을 묶는 RemoteControlOrchestrator skeleton을 control 계층에 추가했다.
```

Files Changed:

```text
firmware/csm/include/board/control/RemoteControlOrchestrator.h
firmware/csm/src/board/control/RemoteControlOrchestrator.cpp
docs/remote/architecture/SOURCE_LAYOUT_PROPOSAL_KO.md
docs/remote/product/CHANGE_HISTORY_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
```

Reason:

```text
RemoteControlSource, AuthorityManager, CommandLimiter, VehicleCommandMapper, CanTxGateway의 호출 순서를
main.cpp에 넣지 않고 별도 control orchestration 경계로 고정하기 위해서다.
```

Boundary:

```text
main.cpp 변경 없음.
platformio.ini 변경 없음.
M4 build env 없음.
UART/Serial3 binding 없음.
actual M4-M7 IPC 없음.
VehicleCommandMapper는 여전히 실제 vehicle frame을 만들지 않음.
CanTxGateway는 여전히 평가만 하고 CAN backend write를 하지 않음.
새 CAN TX 없음.
```

Tests/Evidence:

```text
Boundary search found no CAN TX, D1 gate, HostDownlink, Serial3, MCP2515, digitalWrite, HardwareSerial, UART, HSEM, OpenAMP, or RPC path in control/remote files.
PlatformIO passive env build succeeded and compiled RemoteControlOrchestrator.cpp.
```

Residual Risk:

```text
orchestrator는 아직 main.cpp에 연결되지 않았고, 실제 accepted TX evidence도 없다.
OD-001/002/003/004/005/006이 닫히기 전 production local TX는 여전히 금지된다.
```

Next Step:

```text
Phase 1I: Phase 1 skeleton audit/guard 정리 후 Phase 2 M4 env/Serial3/IPC 증거 수집 계획으로 전환.
```

---

## 2026-07-09: Phase 1I Skeleton Guard And Audit

Summary:

```text
Phase 1 remote skeleton 경계를 자동 점검하는 guard와 Phase 1 closure audit 문서를 추가했다.
```

Files Changed:

```text
firmware/csm/tools/remote_phase1_guard.py
docs/remote/reviews/PHASE1_SKELETON_AUDIT_KO.md
README.md
docs/remote/architecture/SOURCE_LAYOUT_PROPOSAL_KO.md
docs/remote/product/CHANGE_HISTORY_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
```

Reason:

```text
Phase 2에서 M4 env, Serial3, IPC를 열기 전에 Phase 1 skeleton이 main/runtime/UART/IPC/CAN TX로
새지 않았는지 반복 검증할 수 있는 기준을 만들기 위해서다.
```

Boundary:

```text
main.cpp 변경 없음.
platformio.ini 변경 없음.
remote_phase1_guard.py는 PlatformIO extra_scripts에 연결하지 않은 수동 실행 guard다.
새 runtime path 없음.
새 CAN TX 없음.
```

Tests/Evidence:

```text
python firmware/csm/tools/remote_phase1_guard.py passed.
git diff --check passed.
PlatformIO passive env build succeeded.
```

Residual Risk:

```text
Phase 1 guard는 Phase 1 skeleton closure용이다. Phase 2에서 M4 env/IPC/UART를 열면
guard 기준을 Phase 2 evidence profile에 맞게 갱신해야 한다.
```

Next Step:

```text
Phase 2: M4 PlatformIO env proof, Serial3/R16SM capture, M4-M7 IPC prototype evidence.
```

---

## 2026-07-09: Codex-Native AGENTS Routing Restructure

Summary:

```text
CSM Remote 문서/하네스 구조를 Codex 실행 경로 기준으로 재배치하고,
루트 AGENTS.md를 최상위 자동 진입점으로 추가했다.
```

Files Changed:

```text
AGENTS.md
README.md
docs/AGENTS.md
docs/remote/AGENTS.md
firmware/AGENTS.md
firmware/csm/AGENTS.md
firmware/csm/BRIEF.md
firmware/csm/README.md
firmware/csm/board/AGENTS.md
firmware/csm/board/BRIEF.md
firmware/csm/board/.agents/AGENTS.md
firmware/csm/board/docs/AGENTS.md
firmware/csm/docs/AGENTS.md
firmware/csm/install/docs/AGENTS.md
firmware/csm/shared/docs/AGENTS.md
docs/remote/**
```

Reason:

```text
기존 flat root CSM_REMOTE_* 문서 구조는 사람이 보기에는 명확했지만,
Codex 기본 실행성 기준에서는 자동 주입성이 약했다.
또한 imported CSM 내부 AGENTS/BRIEF가 standalone CSM 전제를 갖고 있어 remote 제품 정의와 충돌할 수 있었다.
```

Boundary:

```text
펌웨어 source 변경 없음.
platformio.ini 변경 없음.
imported CSM 문서 삭제 없음.
CSM 문서는 scoped firmware reference로 유지.
remote 제품 권한은 root AGENTS.md와 docs/remote/product/PRODUCT_DEFINITION_KO.md가 소유.
```

Tests/Evidence:

```text
git diff --check passed.
python firmware/csm/tools/remote_phase1_guard.py passed.
root CSM_REMOTE_*.md remaining count: 0.
active router legacy CSM_REMOTE_*.md references: 0.
```

Residual Risk:

```text
AGENTS.md 계층은 Codex 실행 경로에 맞춘 강한 라우팅 장치지만,
모든 모델/도구가 모든 MD를 자동으로 통째로 읽는다는 뜻은 아니다.
따라서 핵심 지침은 루트 AGENTS.md와 scoped AGENTS.md에 짧게 유지해야 한다.
```

Next Step:

```text
Phase 2A: M4 PlatformIO env/build separation proof.
```

---

## 2026-07-09: Phase 2A M4 Remote Frontend Build Proof

Summary:

```text
Portenta H7 M4 전용 build proof env를 추가하고, CRSF parser/RC normalizer/M4 mailbox writer가 M4 target에서 컴파일되는 것을 확인했다.
```

Files Changed:

```text
firmware/csm/platformio.ini
firmware/csm/src/m4_remote_frontend_build_proof.cpp
firmware/csm/tools/remote_phase1_guard.py
firmware/csm/tools/remote_phase2a_guard.py
docs/remote/architecture/SOURCE_LAYOUT_PROPOSAL_KO.md
docs/remote/product/OPEN_DECISIONS_KO.md
docs/remote/product/CHANGE_HISTORY_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
```

Reason:

```text
Phase 2의 첫 증거는 실제 UART/IPC가 아니라 M4 build separation이다.
M4에서 RC parser와 mailbox writer가 컴파일되는지 먼저 확인해야 이후 Serial3/R16SM capture와 IPC prototype을 분리해서 진행할 수 있다.
```

Boundary:

```text
Serial3 binding 없음.
R16SM wiring 없음.
M4-M7 shared memory/IPC 없음.
M7 main.cpp wiring 없음.
VehicleCommandMapper real mapping 없음.
CanTxGateway backend write 없음.
새 CAN TX 없음.
```

Tests/Evidence:

```text
python firmware/csm/tools/remote_phase1_guard.py passed.
python firmware/csm/tools/remote_phase2a_guard.py passed.
PlatformIO M4 env `portenta_h7_m4_remote_frontend_build_proof`: SUCCESS.
PlatformIO M7 passive env `portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive`: SUCCESS.
```

Important Finding:

```text
처음 passive rebuild에서 M4 build proof file이 M7 `+<*>` source filter에 포함되어 setup/loop multiple definition으로 실패했다.
이를 `-<m4_remote_frontend_build_proof.cpp>` exclude와 remote_phase2a_guard source-filter check로 수정했다.
```

Residual Risk:

```text
M4 env는 build proof일 뿐이다.
실제 M4 boot, Serial3 electrical capture, R16SM CRSF decode, shared memory IPC, cache barrier, torn-read evidence는 아직 없다.
```

Next Step:

```text
Phase 2B: R16SM/Serial3 electrical and byte capture plan, then captured CRSF parser test vector execution.
```

---

## 2026-07-09: Phase 2B M4 Serial3 Capture Probe

Summary:

```text
Portenta H7 M4 전용 Serial3 capture probe env를 추가하고,
Serial3 RX byte stream을 CRSF parser/RC normalizer/local mailbox writer까지 통과시키는 lab-only 빌드를 확인했다.
```

Files Changed:

```text
firmware/csm/platformio.ini
firmware/csm/src/m4_remote_serial3_capture_probe.cpp
firmware/csm/tools/remote_phase2b_guard.py
docs/remote/architecture/CRSF_FRONTEND_CONTRACT_KO.md
docs/remote/architecture/SOURCE_LAYOUT_PROPOSAL_KO.md
docs/remote/operations/PHASE2B_R16SM_SERIAL3_CAPTURE_RUNBOOK_KO.md
docs/remote/product/OPEN_DECISIONS_KO.md
docs/remote/product/CHANGE_HISTORY_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
```

Reason:

```text
Phase 2A가 M4 논리 모듈 컴파일 증거라면,
Phase 2B는 M4에서 Arduino `Serial3` API와 CRSF parser 경로가 동시에 컴파일되는지 확인하는 단계다.
제품 runtime이나 차량 명령으로 연결하기 전에 R16SM 하드웨어 캡처를 위한 최소 프로브가 필요하다.
```

Boundary:

```text
lab-only M4 env만 추가.
M7 main.cpp wiring 없음.
M4-M7 shared memory/IPC 없음.
VehicleCommandMapper real mapping 없음.
CanTxGateway backend write 없음.
새 CAN TX 없음.
```

Tests/Evidence:

```text
python firmware/csm/tools/remote_phase1_guard.py passed.
python firmware/csm/tools/remote_phase2a_guard.py passed.
python firmware/csm/tools/remote_phase2b_guard.py passed.
PlatformIO M4 env `portenta_h7_m4_remote_serial3_capture_probe`: SUCCESS.
```

Residual Risk:

```text
아직 실제 R16SM 신호를 캡처하지 않았다.
420000 8N1은 lab default build setting이지 OD-003 closure가 아니다.
Mid Carrier 물리 핀, RX voltage, polarity/inversion, frame cadence, valid hardware decode evidence가 남아 있다.
```

Next Step:

```text
Phase 2B bench execution: R16SM signal을 logic analyzer로 캡처하고,
동일 byte stream을 parser test vector로 재생해 valid frame decode evidence를 남긴다.
```

---

## 2026-07-09: Phase 2C Mailbox Reject Self-Test Expansion

Summary:

```text
RemoteContractSelfTest를 확장해 mailbox torn-write, stale timeout, failsafe sample rejection을 software contract로 확인하도록 했다.
```

Files Changed:

```text
firmware/csm/include/board/remote/RemoteContractSelfTest.h
firmware/csm/src/board/remote/RemoteContractSelfTest.cpp
docs/remote/architecture/M4_M7_REMOTE_MAILBOX_CONTRACT_KO.md
docs/remote/product/OPEN_DECISIONS_KO.md
docs/remote/product/CHANGE_HISTORY_KO.md
docs/remote/product/REQUIREMENT_TRACE_KO.md
```

Reason:

```text
실제 M4-M7 IPC를 열기 전에 M7 reader/decoder가 torn/stale/failsafe 상태를 usable remote source로 승격하지 않는다는
소프트웨어 계약 증거를 강화해야 한다.
```

Boundary:

```text
software self-test helper만 확장.
shared memory address 없음.
cache barrier/HSEM/OpenAMP/RPC 없음.
M7 main.cpp runtime wiring 없음.
새 CAN TX 없음.
```

Tests/Evidence:

```text
python firmware/csm/tools/remote_phase1_guard.py passed.
python firmware/csm/tools/remote_phase2a_guard.py passed.
python firmware/csm/tools/remote_phase2b_guard.py passed.
PlatformIO M7 passive env `portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive`: SUCCESS.
```

Residual Risk:

```text
OD-004는 아직 닫히지 않는다.
실제 dual-core shared memory placement, memory barrier/cache coherency, IPC 선택, hardware torn-read/stale bench evidence가 남아 있다.
```

Next Step:

```text
하드웨어 벤치가 가능하면 R16SM/Serial3 capture를 수행한다.
하드웨어 없이 계속 진행한다면 M4-M7 IPC prototype 설계를 별도 experimental profile로 분리한다.
```
