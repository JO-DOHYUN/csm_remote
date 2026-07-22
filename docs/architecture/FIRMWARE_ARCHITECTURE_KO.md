# CSM 펌웨어 아키텍처

## 코어 책임

### M4

- R16SM UART byte 수신
- CRSF frame bounds/type/CRC 검증
- channel 정규화, link/failsafe/stale 판정
- 고정 크기 double-buffer latest-sample mailbox publish
- CRSF heartbeat와 flight-mode telemetry 송신

M4는 CAN ID, 차량 payload, authority, safety, CAN driver를 알지 않는다.

### M7

- earliest boot evidence capture, `RuntimeSupervisor`, `BootRecovery`
- M4 mailbox 검증과 source 변환
- upstream autonomy 관측
- 단일 authority 및 safety state owner
- command limiting과 vehicle mapping
- 유일한 local CAN TX gateway
- CAN/safety/authority evidence 생성
- canonical uplink publisher와 sink service

## 제어 데이터 흐름

```text
R16SM -> M4 UART/parser/normalizer
      -> fixed latest-sample mailbox
      -> M7 RemoteControlSource
      -> SafetySupervisor
      -> upstream AutonomyAuthorityMonitor
      -> AuthorityManager
      -> CommandLimiter
      -> explicit VehicleCommandMapper
      -> CanTxGateway
      -> CAN backend
      -> CAN_TX_RAW evidence
```

권한 순서는 `hard safety > upstream autonomy > RC > service host > monitoring`이다.
hard safety가 허용되고 autonomy가 `InactiveConfirmed`로 명시적으로 release한
뒤에만 RC reservation을 평가한다. autonomy가 unknown, active, recently active,
ambiguous, protocol fault이면 RC와 service host를 모두 fail-closed로 거부한다.

autonomy release 뒤 RC presence는 neutral qualification 전부터 service host 경계를
예약한다. 500 ms 중립이면 RC handoff가 성립하고, 정상 stale/failsafe는 즉시
중립으로 전환한 뒤 1초 qualification 후 release한다. malformed/protocol/IPC/M4
failure는 authority를 풀지 않는다. SRAM4 IPC는 header, M4 writer, M7 writer
영역을 32-byte cache-line 단위로 분리하며 writer는 자기 영역만 clean한다.

`AuthorityManager`와 mapper는 CAN driver를 호출하지 않는다. `CanTxGateway`는
authority, build profile, allowlist, hardware gate, backend 상태를 검증하고 M7
runtime만 driver write를 수행한다. 현재 Portenta built-in CAN의 양수 write
결과는 driver FIFO enqueue 수락 근거다. FDCAN TX completion/TXBTO와 상관되지
않은 `CAN_TX_RAW`를 물리 bus 송신 성공으로 해석하지 않는다.

Production Remote profile의 mapper 기본값은 `None`이고 local CAN TX capability도
광고하지 않는다. `MdpsBench0x007`은 명시적 bench flag로만 선택할 수 있으며
제품의 5-ID 차량 mapping이 아니다. 실제 vehicle mapping, autonomy runtime wiring,
D1 hardware gate 의미, completion-correlated TX evidence가 승인되기 전에는 이
profile을 차량 제어 release artifact로 판정하지 않는다.

## 관측 데이터 흐름

```text
CAN/RC/authority/safety evidence sources
  -> bounded priority admission
  -> CanonicalPublisher
  -> immutable encoded frame
  -> UsbCdcSink
  -> WifiTcpSink facade -> bounded mailbox -> WifiSocketWorker
```

관측 pipeline은 제어 pipeline의 결과를 읽을 수 있지만 제어 state를 직접 변경하지 않는다.

## 실행 및 메모리 원칙

- hot path는 고정 크기 storage와 bounded queue를 사용한다.
- producer는 transport write를 기다리지 않는다.
- consumer별 진행 상태와 실패 상태를 분리한다.
- `RuntimeSupervisor`는 retained boot-loop 정책을 driver와 분리한다.
- 최종 `main.cpp`는 module construction, profile wiring, deterministic service 호출만 담당한다.
- blocking network 호출, parser body, queue policy, authority policy를 `main.cpp`에 넣지 않는다.

현재 `main.cpp`에는 CAN frontend, typed projection, legacy diagnostic adapter 등
통합 책임이 아직 남아 있다. 이번 변경은 reset/recovery와 Wi-Fi owner 경계를
먼저 추출한 것이며 전체 composition-root 분해가 끝났다고 주장하지 않는다.
분해 시에는 source owner, bounded queue, failure counter, host test seam을 함께
이동하고 임시 facade에 기능을 쌓지 않는다.

## 현재와 목표의 경계

remote product profile은 M4 CRSF frontend, SRAM4 schema 2 IPC, M7
authority/limiter의 read-only vertical slice, built-in CAN 관측, canonical evidence,
USB/Wi-Fi 독립 sink까지 연결됐다. 그러나 autonomy 입력은 제품 runtime에 아직
연결되지 않았고 mapper는 `None`, local application-data CAN TX capability는
기본 Off다. 기존 passive
profile은 회귀 기준으로 유지한다. reset/recovery 상세 경계는
`DEBUG_AND_RECOVERY_ARCHITECTURE_KO.md`를 따른다.
