# CSM Firmware Architecture

Authority: `L2 ACTIVE ARCHITECTURE / BINDING IN IMPLEMENT / REVIEWABLE IN ARCH_CHANGE`

Machine-readable owner/model은 `ACTIVE_ARCHITECTURE.yaml`이 소유한다. 이 문서의
processor, timing, FIFO, queue와 class detail은 Product Constitution이 아니다.

## Active Artifacts

| Role | PlatformIO environment |
|---|---|
| CSM M7 Service/HIL product | `portenta_h7_m7_mid_feeder_uart_j4_remote_service_hil_wifi` |
| paired RC frontend | `portenta_h7_m4_remote_frontend` |

M7/M4는 같은 source/contract identity를 사용해 paired artifact로 배포한다. Feeder
firmware baseline 자체는 이번 active manifest에서 unresolved이며 CSM은 feeder wire
입력만 검증한다.

## Processor Ownership

### M4 RC frontend

- R16SM UART/CRSF byte/frame/CRC 검증
- channel/link/failsafe/stale 정규화
- 고정 latest-sample SRAM mailbox publish
- RC telemetry response

M4는 vehicle CAN ID/payload, 공통 authority, hard-safety, CAN driver와 Host control을
소유하지 않는다.

### M7 product executive

- reset/runtime evidence와 watchdog supervision
- M4 mailbox 및 feeder-UART input 검증
- hard-safety와 단일 authority state
- RC/autonomy semantic limiter·mapper·release
- Service/HIL Host raw admission
- 유일한 built-in FDCAN owner와 completion journal
- canonical typed publisher와 독립 USB/Wi-Fi sink

## Current Control Flow

```text
R16SM -> M4 CRSF frontend -> fixed mailbox
  -> M7 RC source -> authority + safety -> limiter -> vehicle mapper
  -> release schedule -> sole FDCAN owner -> HW completion

Android Service/HIL raw request
  -> Wi-Fi epoch/parser
  -> sender freshness/replay + Host authority/lease + hard-safety
  -> static frame validation
  -> one immediate tracked FDCAN HW admission attempt
  -> sole FDCAN owner -> HW terminal
```

RC/autonomy는 semantic path를 사용한다. Service/HIL Host path에서는 Android가 vehicle
meaning, sequence와 nominal absolute request timeline을 소유하고 CSM이 payload를
byte-preserve한다. 두 path는 같은 authority/safety와 physical owner 아래에서만 실행된다.

## Host Raw Execution

- active allowlist: advertised built-in bus, standard `0x005/0x007/0x364`, DLC8, RTR/EXT false
- one accepted record -> one immediate tracked HW FIFO submission
- Host software execution FIFO, ID cadence/phase lane, latest overwrite, hidden retry/replay 없음
- busy/journal/full/pre-HW failure는 그 request의 typed ACK reject이며 저장하지 않는다.
- `CONTROL_ACK Accepted`는 tracked HW admission만 의미한다.
- command-correlated terminal은 `CONTROL_TX_EVIDENCE` schema 2다.
- physical completion stream은 `CAN_TX_RAW`이며 payload로 command ID를 추정하지 않는다.

Host authority 상실은 새 admission close -> freshness/lease epoch close -> reasoned Host
HW cancellation -> terminal drain -> RC allow 순서다. final terminal 전 Host/RC overlap을
허용하지 않고, zero/final slot closure 뒤 인위적 wait도 추가하지 않는다. hard-safety는
모든 application-control origin cancellation을 요청한다.

Terminal outcome은 `Transmitted`, `IntentionalCancelled`, `HardwareFailure`,
`TrackingFailure`를 구분한다. intentional cancellation은 HW failure counter/global
inhibit로 합치지 않는다. owner slot은 실제 terminal truth 전까지 지우지 않는다.

## RC Semantic Execution

RC latest state는 M7 authority/safety, limiter, mapper와 current absolute release
schedule을 통과한다. drive/steering release, reversal/neutral과 safety-stop behavior는
RC semantic path 소유이며 Host raw payload에 적용하지 않는다. RC parsing/telemetry는
Host가 active/draining인 동안에도 계속되지만 RC/SafetyNeutral CAN origin은 Host terminal
closure 전 physical owner에 제출되지 않는다.

## CAN Receive and Canonical Evidence

```text
feeder UART DMA/COBS/CRC/sequence -> CAN_RX bus0
built-in FDCAN RX ISR/ring       -> CAN_RX bus1
authority/safety/control/HW      -> typed evidence
  -> one EvidenceSequencer / CanonicalPublisher
       -> bounded USB sink
       -> bounded Wi-Fi sink
```

canonical bytes와 publish identity는 sink 앞에서 한 번 생성된다. 어느 sink도 CAN ingest,
authority, safety, physical TX 또는 다른 sink를 기다리게 하지 않는다. Queue admission
miss, close, epoch, high-water와 loss range는 explicit evidence다.

## Executive Priority and Boundedness

1. HW completion/cancellation truth
2. RC/hard-safety/authority
3. Host downlink admission
4. feeder/CAN ingest
5. canonical publication
6. bounded USB/Wi-Fi draining

hot path는 heap allocation, unbounded queue, blocking telemetry dependency를 두지 않는다.
Main loop는 completion/control/Host를 bulk drain 전후 bounded poll하고 Wi-Fi worker에는
nonblocking yield만 제공한다.

## Active Qualification State

- sender-time, completion and transient thresholds: `Exploratory`
- product authority for experiment values: false
- enabled source calculation: `1,095 records/s`, `131,513 B/s`
- exact current constants/flags: source, platformio.ini, `CAPABILITY`

Threshold는 exploratory measurement -> reviewed value -> product constant freeze ->
qualification HIL 순서로만 승격한다. Source/build success는 physical timing/ACK proof가 아니다.

## Source Owners and Verification

| Responsibility | Primary source |
|---|---|
| authority | `include/src/board/authority` |
| RC/Host control | `include/src/board/control`, `HostDownlinkParser` |
| physical FDCAN | `BuiltinCanTxOwner`, `BuiltinFdcanDiagnostics` |
| feeder input | `FeederUartIngress`, `FeederWireProtocol` |
| publisher/sinks | `include/src/board/uplink` |
| typed schema | `include/protocol`, canonical wire document |

Required static gates are Constitution, architecture conformance, experiment and Wi-Fi architecture
guards plus affected native contracts. Exact M7/M4 build proves compilation only. Device/HIL success
requires actual artifact upload and correlated external evidence.
