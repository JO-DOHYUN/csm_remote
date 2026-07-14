# CSM Remote Source Layout Proposal

작성일: 2026-07-09
상태: Phase 0 proposal
대상 코드: `firmware/csm`
상위 기준: `docs/remote/product/PRODUCT_DEFINITION_KO.md`, `docs/remote/reviews/PHASE0_HANDOFF_REVIEW_KO.md`

이 문서는 CSM Remote 구현을 시작하기 전, 최종 모듈 배치와 데이터 흐름을 정의한다.
목표는 `main.cpp`에 기능을 몰아넣지 않고, 최종 아키텍처의 뼈대부터 만든 뒤 내부를 채우는 것이다.

---

## 1. 현재 CSM 구조

현재 subtree 기준 주요 구조:

```text
firmware/csm/
  platformio.ini
  include/
    BoardPins.h
    board/
      CapabilityPublisher.h
      ControlPolicy.h
      HostDownlinkParser.h
      SafetySupervisor.h
      StatusLed.h
      can/
        CanTypes.h
        ICanBackend.h
      uplink/
        CanRxSegmentBuilder.h
        SerialTxScheduler.h
        UplinkPriority.h
        UplinkPriorityPolicy.h
        UplinkScheduler.h
    protocol/
      HostCommands.h
      TypedFrame.h
      TypedRecords.h
  src/
    main.cpp
    board/
      CapabilityPublisher.cpp
      ControlPolicy.cpp
      HostDownlinkParser.cpp
      SafetySupervisor.cpp
      StatusLed.cpp
      uplink/
        ...
    protocol/
      TypedFrame.cpp
  tools/
    passive_guard.py
```

현재 구조의 의미:

```text
include/board + src/board 패턴이 이미 있다.
새 M7 모듈도 이 패턴을 따라야 한다.
```

---

## 2. Source Layout 원칙

### 2.1 최종 아키텍처 우선

구현 순서:

```text
1. directory/file layout
2. public type/interface
3. deny-first skeleton
4. dataflow wiring
5. tests/guards
6. logic fill
```

### 2.2 main.cpp 밀집 금지

`firmware/csm/src/main.cpp`는 다음만 담당한다.

```text
module construction
top-level setup/loop orchestration
profile wiring
legacy integration bridge
```

`main.cpp`에 넣지 않을 것:

```text
CRSF parser body
M4-M7 mailbox body
AutonomyAuthorityMonitor body
AuthorityManager policy body
VehicleCommandMapper body
CanTxGateway policy body
remote evidence correlation body
```

### 2.3 기존 모듈 존중

기존 모듈을 대체하지 않는다.

```text
SafetySupervisor remains source-agnostic safety gate.
ControlPolicy remains existing host/control policy artifact until replaced by a new mapper/gateway path.
Uplink modules remain typed record scheduling/serialization path.
HostDownlinkParser remains bench/full-instrumented path, not product remote authority path.
```

---

## 3. Phase 1 M7 Skeleton Layout

Phase 1에서 추가할 수 있는 파일:

```text
firmware/csm/include/board/authority/
  AuthorityTypes.h
  AutonomyAuthorityMonitor.h
  AuthorityManager.h

firmware/csm/src/board/authority/
  AutonomyAuthorityMonitor.cpp
  AuthorityManager.cpp

firmware/csm/include/board/control/
  OperatorCommand.h
  CommandLimiter.h
  VehicleCommandMapper.h
  CanTxGateway.h
  RemoteControlOrchestrator.h

firmware/csm/src/board/control/
  CommandLimiter.cpp
  VehicleCommandMapper.cpp
  CanTxGateway.cpp
  RemoteControlOrchestrator.cpp

firmware/csm/include/board/remote/
  RemoteTypes.h
  CrsfParser.h
  RcNormalizer.h
  M4RemoteMailboxContract.h
  M4RemoteMailboxWriter.h
  M4RemoteMailboxReader.h
  RemoteContractSelfTest.h
  RemoteControlSource.h

firmware/csm/src/board/remote/
  CrsfParser.cpp
  RcNormalizer.cpp
  M4RemoteMailboxContract.cpp
  M4RemoteMailboxWriter.cpp
  M4RemoteMailboxReader.cpp
  RemoteContractSelfTest.cpp
  RemoteControlSource.cpp
```

Phase 1에서 선택적으로 추가할 수 있는 파일:

```text
firmware/csm/include/board/host/
  HostServiceSource.h

firmware/csm/src/board/host/
  HostServiceSource.cpp
```

다만 HostServiceSource는 product default disabled로 시작한다.

---

## 4. Phase 2 M4 Frontend Layout

Phase 2A에서 M4 build proof 전용 파일을 먼저 생성했다.

현재 생성:

```text
firmware/csm/src/
  m4_remote_frontend_build_proof.cpp
  m4_remote_serial3_capture_probe.cpp
```

목적:

```text
Portenta H7 M4 PlatformIO env에서 CRSF parser, RC normalizer, M4 mailbox writer가
컴파일되는지 증명한다.
```

Phase 2B 목적:

```text
Portenta H7 M4 PlatformIO env에서 Serial3 RX capture probe가 컴파일되는지 증명한다.
R16SM byte stream을 CRSF parser -> normalizer -> local mailbox frame writer까지 통과시키되,
차량 명령, M7 runtime, shared memory, CAN TX에는 연결하지 않는다.
```

금지:

```text
product Serial3 runtime enablement 없음.
R16SM wiring 확정 없음.
M4-M7 shared memory/IPC 없음.
CAN TX 없음.
M7 main.cpp wiring 없음.
```

후보 구조:

```text
firmware/csm/src_m4/
  main_m4.cpp
  remote/
    CrsfUartDriver.cpp
    CrsfParser.cpp
    RcNormalizer.cpp
    RcFailsafeDetector.cpp
    RemoteMailboxWriter.cpp

firmware/csm/include/remote_m4/
  CrsfTypes.h
  CrsfParser.h
  RcNormalizer.h
  RemoteMailbox.h
```

대안:

```text
firmware/csm/src/remote_m4/
```

최종 선택은 PlatformIO M4 env와 source_filter 확인 후 결정한다.

Phase 2A 결과:

```text
PlatformIO env: portenta_h7_m4_remote_frontend_build_proof
M4 board target: portenta_h7_m4
M4 build proof: passed
M7 passive build after source-filter isolation: passed
```

Phase 2B 결과:

```text
PlatformIO env: portenta_h7_m4_remote_serial3_capture_probe
M4 board target: portenta_h7_m4
Serial3 capture probe build: passed
Default capture baud: 420000 8N1
M7 source-filter isolation: protected by remote_phase2b_guard.py
```

남은 open item:

```text
OD-004 M4-M7 IPC mechanism
R16SM logic analyzer capture and valid hardware decode evidence
Mid Carrier physical Serial3 connector/pin evidence
M4/M7 build output separation
```

---

## 5. Dataflow별 소유 모듈

### 5.1 RC input flow

```text
M4 RemoteRxFrontend
  -> RemoteMailbox
  -> M7 M4RemoteMailboxReader
  -> RemoteControlSource
  -> OperatorCommand
```

소유:

| Data | Owner |
|---|---|
| CRSF bytes | M4 CrsfUartDriver |
| CRSF frame | M4 CrsfParser |
| normalized channel | M4 RcNormalizer |
| RcSample | RemoteMailbox contract |
| OperatorCommand | RemoteControlSource |

금지:

```text
RemoteControlSource cannot know CAN IDs.
M4 cannot know CAN IDs or payloads.
```

### 5.2 Autonomy observe flow

```text
CAN RX
  -> AutonomyAuthorityMonitor
  -> AutonomyAuthorityState
  -> AuthorityManager
```

소유:

| Data | Owner |
|---|---|
| upstream frame classification | AutonomyAuthorityMonitor |
| local_tx_inhibit_latch | AutonomyAuthorityMonitor |
| local source ownership | AuthorityManager |

금지:

```text
RemoteControlSource cannot evaluate autonomy release.
VehicleCommandMapper cannot evaluate autonomy release.
```

### 5.3 Local command flow

```text
OperatorCommand
  -> AuthorityManager
  -> SafetySupervisor
  -> CommandLimiter
  -> VehicleCommandMapper
  -> CanTxGateway
  -> CAN backend
```

소유:

| Data | Owner |
|---|---|
| source authority | AuthorityManager |
| board safety | SafetySupervisor |
| command rate/limit | CommandLimiter |
| vehicle frame candidate | VehicleCommandMapper |
| final local CAN TX permission | CanTxGateway |

금지:

```text
AuthorityManager cannot call CAN driver.
VehicleCommandMapper cannot call CAN driver.
Only CanTxGateway may call the final local motion CAN TX path.
```

---

## 6. Interface Type 배치

### 6.1 AuthorityTypes.h

포함:

```cpp
enum class AutonomyAuthorityState : uint8_t;
enum class AuthorityState : uint8_t;
enum class ControlSourceId : uint8_t;
enum class ControlDecisionCode : uint8_t;
struct AutonomyCommandProfile;
struct AuthorityDecision;
```

금지:

```text
CAN frame payload data
CRSF parser internals
VSM UI concepts
```

### 6.2 RemoteTypes.h

포함:

```cpp
enum class RemoteLinkState : uint8_t;
enum class RcSampleState : uint8_t;
struct RcSample;
```

금지:

```text
CAN ID
CAN payload
AuthorityManager state mutation
```

### 6.3 OperatorCommand.h

포함:

```cpp
struct OperatorCommand;
```

금지:

```text
CAN ID
raw CAN bytes
driver handle
```

### 6.4 CanTxGateway.h

포함:

```cpp
struct CanFrameRequest;
class CanTxGateway;
```

허용:

```text
CanFrameRequest는 VehicleCommandMapper 이후에만 생성된다.
```

---

## 7. Build Profile Proposal

### 7.1 Phase 1 기본값

Phase 1은 remote skeleton을 compile 가능하게 만들 수 있지만, 동작은 꺼져 있어야 한다.

후보 flag:

```ini
-D BOARD_ENABLE_REMOTE_CONTROL=0
-D BOARD_ENABLE_M4_REMOTE_FRONTEND=0
-D BOARD_ENABLE_REMOTE_AUTHORITY_SKELETON=1
```

주의:

```text
PassiveProduct build에는 active remote path가 없어야 한다.
Skeleton type/header는 있어도 active symbol guard에 걸리지 않게 설계한다.
```

### 7.2 Remote candidate env

Phase 1 후반 또는 Phase 2에서 추가:

```ini
[env:portenta_h7_m7_mid_mcp2515_j4_dual_csm_remote_candidate]
extends = env:portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive
build_flags =
  ...
  -D BOARD_CSM_PROFILE_PASSIVE_PRODUCT=0
  -D BOARD_ENABLE_REMOTE_CONTROL=1
  -D BOARD_ENABLE_HOST_CAN_TX=0
  -D BOARD_ENABLE_HOST_DOWNLINK=0
```

이 env도 처음에는 local CAN TX deny-first여야 한다.

---

## 8. Guard Proposal

기존:

```text
firmware/csm/tools/passive_guard.py
```

추가 후보:

```text
firmware/csm/tools/remote_phase1_guard.py
```

remote_phase1_guard 검사:

```text
remote/control skeleton contains direct UART/IPC/CAN TX IO -> fail
remote module knows CAN ID/frame/gateway types -> fail
main.cpp wires Phase 1 remote runtime -> fail
platformio.ini enables M4/remote runtime before Phase 2 -> fail
VehicleCommandMapper maps real vehicle frames -> fail
CanTxGateway performs backend write -> fail
```

Phase 1I에서 수동 실행 guard로 추가했다. PlatformIO extra_scripts에는 아직 연결하지 않는다.

---

## 9. Phase 1 작업 단위

Phase 1A:

```text
AuthorityTypes.h
RemoteTypes.h
OperatorCommand.h
deny-first enums and structs
```

Phase 1B:

```text
AutonomyAuthorityMonitor skeleton
AuthorityManager skeleton
RemoteControlSource skeleton
M4RemoteMailboxReader skeleton
```

Phase 1C:

```text
CommandLimiter skeleton
VehicleCommandMapper skeleton
CanTxGateway skeleton
```

Phase 1D:

```text
M4-M7 mailbox fixed frame contract
torn/corrupt/protocol fault decode skeleton
compile integration
Requirement Trace update
```

Phase 1E:

```text
platform-independent CRSF frame parser skeleton
RC channel normalizer skeleton
no UART binding, no M4 env, no channel/switch semantic closure
```

Phase 1F:

```text
platform-independent M4 mailbox writer helper
RcSample -> M4RemoteMailboxFrame pack/CRC/sequence skeleton
no shared memory address, no barrier/HSEM/OpenAMP/RPC binding
```

Phase 1G:

```text
compile-ready remote contract self-test helper
synthetic CRSF -> normalize -> mailbox writer -> mailbox reader roundtrip
not wired to main.cpp and not hardware/IPC evidence
```

Phase 1H:

```text
M7 remote control orchestration skeleton in control layer
RemoteControlSource -> AuthorityManager -> CommandLimiter -> VehicleCommandMapper -> CanTxGateway
no main.cpp wiring, no CAN backend write
```

Phase 1I:

```text
remote_phase1_guard.py
Phase 1 skeleton audit document
Phase 2 evidence/prototype entry criteria
```

Phase 1 금지:

```text
new real local CAN TX
vehicle CAN mapping
M4 UART/Serial3 runtime binding
M4-M7 real IPC binding
Host/VSM control expansion
```

---

## 10. 최종 제안

Phase 1은 다음 layout으로 시작한다.

```text
include/board/authority
src/board/authority
include/board/control
src/board/control
include/board/remote
src/board/remote
```

이 layout은 현재 CSM의 `include/board` + `src/board` 패턴과 맞고,
나중에 M4 frontend를 분리해도 M7 authority path가 흔들리지 않는다.

다음 작업:

```text
Phase 1A: AuthorityTypes / RemoteTypes / OperatorCommand 작성
Passive build 유지 확인
main.cpp 변경 최소화 또는 없음
```
