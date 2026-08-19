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
      -> M7 RemoteControlSource ----\
ServiceHil authenticated intent -----+-> RealtimeCoordinator
Autonomy runtime provider ----------/
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
authority, build profile, allowlist, safety, backend 상태를 검증하고 M7
runtime만 driver write를 수행한다. 현재 Portenta built-in CAN의 양수 write
결과는 driver FIFO enqueue 수락 근거다. FDCAN TX completion/TXBTO와 상관되지
않은 `CAN_TX_RAW`를 물리 bus 송신 성공으로 해석하지 않는다.

Production Remote profile의 mapper 기본값은 `None`이고 local CAN TX capability도
광고하지 않는다. `VehicleBench0x005And0x007`은 명시적 bench flag로만 선택할 수 있으며
제품의 5-ID 차량 mapping이 아니다. 실제 vehicle mapping, autonomy runtime wiring,
실제 vehicle safety 입력과 completion-correlated TX evidence가 승인되기 전에는 이
profile을 차량 제어 release artifact로 판정하지 않는다.

compile-time `InactiveConfirmed`와 control-enable flag는 runtime evidence가 아니다.
RemoteProduct는 실제 autonomy provider가 fresh `InactiveConfirmed`를 제공하고,
approved vehicle profile과 실제 authority/safety evidence가 모두 유효할 때만 local
motion authority를 검토한다. 입력이 없는 경우 `Unknown/inhibit`가 정상 제품
동작이다. bench-only release adapter는 capability/profile에 별도로 표시한다.

ServiceHil의 wire request는 authority/safety 우회 permission이 아니다. Host raw
path는 configured bus, standard ID/DLC/RTR allowlist를 검증하고 payload를
byte-preserve한 뒤 sole `BuiltinCanTxOwner`의 실제 3-slot FDCAN FIFO에 즉시 한 번
제출한다. vehicle meaning, nominal cadence, sequence, count, ramp, CENTER, EHB와
explicit neutral은 upper control SW가 소유한다. CSM은 Host 실행 FIFO·retry·retiming을
두지 않고 현재 HW admission 불가를 명시적으로 reject한다.
Host가 FDCAN driver를 직접 호출하거나 CSM이 request를 semantic intent로 변환하는
두 경로 모두 금지한다.

ServiceHil ARM은 명시적 operator 요청, 현재 epoch/boot identity, fresh health,
RC/autonomy release, heartbeat/lease와 CAN backend ready를 요구한다. 존재하지 않는
외부 ArmKey 또는 CAN-TX gate 입력은 제품 계약으로 가정하지 않는다.

vehicle 벤치에는 별도 `remote_product_mdps_bench_wifi` artifact를 사용한다. 이
artifact도 제품 authority/safety/limiter와 canonical USB/Wi-Fi 경계를 그대로
사용하지만 J4의 drive `0x005` 200 Hz와 steering `0x007` 50 Hz mapper만 명시적으로 연다. MCP2515는 normal-mode
RX/ACK용이며 MCP/host control TX는 허용하지 않는다. 따라서 이 artifact의 통과는
MDPS 벤치 근거이지 실제 차량 mapping 또는 release 승인 근거가 아니다.

successor feeder 하드웨어의 앱 제어 시험에는 별도
`portenta_h7_m7_mid_feeder_uart_j4_remote_service_hil_wifi` artifact를 사용한다.
이 artifact는 feeder CAN0 ingress, M4 RC, J4 CAN1, pinned product Mbed를
유지하면서 firmware profile 2와 Wi-Fi host downlink만 명시적으로 연다.
RC와 service host는 같은 authority/safety/0x005·0x007 allowlist를 통과하며
동시에 motion owner가 될 수 없다. observer artifact에는 이 경로를 추가하지 않는다.

## 관측 데이터 흐름

```text
bus0: RP2040 feeder MCP25625/SPI -> COBS+CRC32C UART -> M7 circular DMA
bus1: Mid Carrier J4 built-in CAN
RC/authority/safety evidence sources
  -> bounded priority admission
  -> CanonicalPublisher
  -> immutable encoded frame
  -> UsbCdcSink
  -> WifiTcpSink facade -> bounded mailbox -> WifiSocketWorker
```

관측 pipeline은 제어 pipeline의 결과를 읽을 수 있지만 제어 state를 직접 변경하지 않는다.

successor feeder profile에서 bus0의 SPI/CAN ingest와 ACK는 RP2040이 소유한다.
M7은 feeder로 TX하지 않으며 UART packet의 boot ID, packet/frame sequence, CRC,
source counter를 검증한 뒤에만 기존 CAN truth queue에 넣는다. feeder UART sink
정체는 RP2040 core0의 CAN ingest를 막지 않고, CSM USB/Wi-Fi sink 정체는 UART
DMA ingest를 막지 않는다. J4 bus1, RC authority, safety, control TX 소유권은
기존 M7 경계를 유지한다.

feeder bus0 readiness는 단순 UART packet 수신으로 성립하지 않는다. 현재 boot
ID에서 새로 수신한 status가 250 ms 이내이고, packet도 stale하지 않으며, source
ring overflow/MCP overflow/error IRQ/bus-off/EFLG와 M7 DMA cursor fault가 모두
없어야 한다. boot ID 변경은 이전 status와 그 timestamp를 즉시 무효화한다.
이 readiness 전이는 capability, BOARD_HEALTH, status LED에 동일하게 투영한다.

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

## 최종 결정론적 product executive

제품 경로의 최종 소유권과 데이터 흐름은 아래 하나로 고정한다.

```text
hard-safety ISR ---------------------------> latched safety snapshot
M4 RC latest / autonomy latest
  -> RealtimeCoordinator (absolute release timeline)
  -> authority -> safety -> limiter -> mapper
  -> FdcanOwner -> hardware TX completion journal

HostCanTxRequest
  -> sender-time freshness/replay + session/authority/lease/hard-safety
  -> static frame allowlist -> immediate tracked HW FIFO admission
  -> FdcanOwner -> hardware TX completion journal

FDCAN RX ISR ring / feeder UART DMA ring
  -> source validator -> EvidenceSequencer
  -> CanonicalPublisher
  -> independent USB worker / independent Wi-Fi worker
```

- `RealtimeCoordinator`는 RC/autonomy semantic state와 release frame을 소유한다.
  Host raw admission은 semantic coordinator를 통과하지 않지만 동일 M7 authority,
  safety와 sole FDCAN owner를 우회하지 않는다.
- Host raw path에는 software execution backlog와 ID별 scheduler가 없다. parser는
  stream order로 record를 복원하고 각 fresh request를 실제 3-slot HW FIFO에 한 번만
  제출한다. Accepted는 tracked HW admission이며 busy/full reject는 저장·재시도되지
  않는다. transport epoch/authority/safety closure는 아직 pending인 HW request에
  bounded cancellation을 요청하되 terminal truth 전까지 owner slot을 지우지 않는다.
- `FdcanOwner`만 built-in FDCAN register/FIFO를 소유한다. enqueue 성공과 실제
  TX 완료를 구분하고 `CAN_TX_RAW` 성공 evidence는 hardware completion 뒤에만
  생성한다.
- feeder는 외부 CAN의 read-only source다. UART DMA 수신, COBS/CRC, boot/packet/
  frame sequence 검증을 통과한 frame만 canonical input이 된다.
- `EvidenceSequencer`는 sink 수락 여부와 무관하게 canonical record identity를
  한 번 소비한다. USB와 Wi-Fi는 immutable record를 독립 소비하며 어느 sink도
  source ingest, authority 또는 control deadline을 막지 않는다.

## 시간과 과부하 계약

- 기준 timeline은 1 ms absolute phase이며 drive 5 ms, steering 20 ms release는
  `previous_release + period`로 진행한다. 지연 뒤 `now + period`로 재기준화하지
  않고, 놓친 release 수를 정확히 계수하며 catch-up burst는 만들지 않는다.
- 한 control job이 다음 release와 겹치면 timing fault를 기록하고 안전 정책에
  따라 inhibit/neutral로 전이한다. watchdog은 단순 loop 생존이 아니라 ordered
  checkpoints와 control progress를 감시한다.
- hard safety는 atomic latched state, 연속 조작값은 latest snapshot, arm/session/
  completion/fault는 bounded ordered event로 분리한다.
- source별 ring은 single-producer/single-consumer 소유권을 갖는다. hot path에는
  heap allocation, mutex 대기, transport retry/yield를 두지 않는다.
- feeder UART decoder는 한 loop에서 byte budget과 CPU-time budget을 동시에
  적용한다. 기본 CPU budget은 750 us이고 64-byte 이하 chunk 사이에서만
  재평가하여 control release를 보호한다. circular DMA의 NDTR reload와
  transfer-complete callback 사이 race는 pending TC가 증명하는 한 번의 wrap만
  보정한다. 그 밖의 역행 cursor는 byte replay를 하지 않고 ingress epoch를
  fail-closed로 중단한다.
- DMA error ISR은 atomic error event/count만 release-publish한다. restart state,
  parser reset과 ingress statistics는 main owner가 acquire/exchange 후 처리한다.
  ISR과 main이 plain mutable state를 함께 쓰지 않는다.
- Wi-Fi worker는 설정된 write/byte budget 안에서 먼저 배수한다. 실제
  `WOULD_BLOCK` 또는 무진행 시간/고수위가 함께 성립할 때만 해당 client를
  격리하고 새 sink epoch로 재연결한다.

현재 검증된 feeder 운용 envelope는 UART 1,000,000 baud에서 2,000 CAN
frame/s다. 더 높은 rate나 양방향 feeder는 wire/link budget, DMA overrun,
fault-injection, soak gate를 새로 통과하기 전까지 제품 계약이 아니다.

## 단계별 완성 gate

1. absolute periodic scheduler와 Wi-Fi bounded drain을 host contract로 고정한다.
2. FDCAN 단일 owner와 direct-write 금지 guard를 세운다.
3. RX ISR ring/FIFO-lost evidence와 TX completion journal을 연결한다.
4. RC/autonomy semantic 경로와 Service/HIL Host raw 경로를 authority/safety 및
   sole FDCAN owner 앞에서 합류시키고 mapper ownership은 분리한다.
5. feeder reset, CRC, truncation, duplicate/reorder, ring/UART overflow를 주입한다.
6. legacy direct path를 제거하고 `main.cpp`를 composition root로 축소한다.
7. 동시 RC + feeder 2,000 frame/s + USB + Wi-Fi fault/soak를 통과한 뒤 제품
   readiness를 주장한다.

## artifact compatibility

M7, M4와 feeder는 protocol version 외에 release bundle contract ID, build ID,
profile ID를 함께 제공한다. M7은 현재 boot에서 fresh한 identity가 모두 일치하기
전까지 해당 source를 ready로 승격하지 않는다. exact binary SHA-256와 build
manifest는 release tooling이 생성하며 filename이나 build 시각만으로 pair를
추정하지 않는다.

HNO1 Rev 0 workbook은 Driving Line 1 Mbit/s와 System Line 500 kbit/s의 draft
provenance다. 현재 `BENCH_005_007_V1` payload와 같은 ID의 의미가 충돌하므로
vehicle profile ID/contract hash/role/bitrate가 일치하지 않으면 decode와 TX를
차단한다. 승인된 generated control package 전에는 HNO1 control mapping을 만들지
않는다.
