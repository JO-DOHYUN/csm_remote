# CSM Uplink Transport 아키텍처

Updated: 2026-08-03

Authority: `L2 DOMAIN ARCHITECTURE / BINDING IN IMPLEMENT / REVIEWABLE IN ARCH_CHANGE`

Current cross-repository model은 `ACTIVE_ARCHITECTURE.yaml`과 함께 읽는다.

이 문서는 현재 CSM 제품 데이터 경계의 권위 문서다. 과거 retained
journal/APP ACK/replay 실험은
`history/decisions/DECISION_LEDGER_KO.md`에 HISTORY로만 남긴다.

현재 상태는 `IMPLEMENTED CANDIDATE / RELEASE BLOCKED`다. 2026-08-03의
transient-envelope 변경은 host 계약, 제품 build, 정확 artifact 업로드와 PC
dual-sink 4,000 fps gate까지 통과했다. Android+Capture, RC/CAN-TX 동시 부하,
fault injection과 soak 전에는 release-qualified로 표시하지 않는다.

## 제품 우선순위와 시작 순서

제품 시작 순서는 다음으로 고정한다.

```text
1. 모든 차량 출력 safe/inhibit
2. reset/watchdog evidence
3. M4 RC freshness와 M7 authority/safety
4. CAN ingest와 승인된 CAN TX
5. USB diagnostic
6. Wi-Fi observer
```

Wi-Fi 시작 실패, client 정지, queue overflow 또는 socket stall은 RC,
authority, CAN, canonical publication과 USB 시작을 막지 않는다. 내부 Wi-Fi는
같은 M7/kernel/SDIO/전원을 공유하므로 논리적 sink 격리만 주장할 수 있다.

## 최종 데이터 흐름

```text
M4 CRSF latest sample --------\
RP2040 feeder UART CAN0 -------+--> M7 authority/safety/CAN executive
J4 FDCAN CAN1 ----------------/                  |
                                          RecordAdmission
                                                |
                                        CanonicalPublisher
                                    (identity 1회, encode 1회)
                                       /                  \
                            USB live FIFO          Wi-Fi live FIFO
                            192 descriptors        256 descriptors
                              40,960 B                49,152 B
                                                       |
                                             one RTOS socket worker
                                                       |
                                            WHD SoftAP + raw TCPSocket
                                                       |
                                         Android ordered SessionCore
                                            /                  \
                                      current Live       independent Capture
```

- RC, authority, safety, CAN ingest와 canonical publisher는 network API를
  호출하거나 기다리지 않는다.
- USB와 Wi-Fi는 독립 queue, epoch, progress, drop counter를 소유한다.
- canonical bytes와 `publish_seq64`는 sink 앞에서 한 번만 생성된다.
- Android Capture 성공 여부는 CSM admission, TCP epoch와 Live 송신에
  영향을 주지 않는다.
- CSM은 network backlog replay나 Android 파일 durability를 제공하지 않는다.

## USB live FIFO 계약

USB는 `192 descriptors / 40,960 encoded bytes`의 byte-ring FIFO를 사용한다.
기존 8-record queue처럼 모든 slot에 최대 frame 크기를 고정 할당하지 않는다.
현재 enabled schema 계산은 `131,513 B/s / 1,095 records/s`지만 transient
coverage threshold는 exploratory 상태이며 product interval로 동결되지 않았다.
USB host backpressure는 CAN ingest, canonical publisher와 Wi-Fi sink를 막지 않는다.
실제 overflow는 `BOARD_HEALTH.usb_overflow`, canonical sequence gap과 sink
counter로 숨김없이 판정한다.

## Wi-Fi live FIFO 계약

Wi-Fi queue는 `256 descriptors / 49,152 encoded bytes`의 bounded live
FIFO다. 그중 `4 descriptors / 2,112 bytes`는 critical evidence에 예약되어
normal admission은 `252 records / 47,040 bytes`다. current threshold state는
exploratory이고 transient coverage는 `0`으로 광고된다. 측정·review·constant
freeze·qualification 전에는 이 queue에서 제품 지속 처리량이나 coverage interval을
추론하지 않는다. 이는 단절 구간을 보존하는 journal이 아니며 지속 처리량 부족을 숨기지 않는다.

- producer offer는 단 한 번의 nonblocking 시도만 한다.
- positive socket return은 해당 byte를 즉시 소비한다. 완성 record는 즉시
  해제하고 partial write면 전송되지 않은 suffix만 유지한다.
- admission 누계는 producer mailbox가 소유하고 worker는 자신의 sent/abort
  누계와 결합해 `pending = accepted - sent - aborted`인 단일 coherent
  snapshot을 발행한다. 진단은 물리 FIFO 즉석값과 이 누계를 섞지 않는다.
- TCP ACK 외에 CSM RAM 회수를 허가하는 application ACK는 없다.
- disconnect에서 send cursor를 rewind하거나 과거 record를 replay하지 않는다.
- `32,768 bytes 또는 192 records` high-water는 drain을 재촉하고 계측하는
  상태일 뿐 close 조건이 아니다. 둘 다 `8,192 bytes 이하 및 64 records 이하`로
  내려와야 pressure 상태가 해제된다.
- normal reserve 또는 실제 full에서 첫 record가 수락되지 못하면 그 정확한
  `publish_seq64`를 loss로 고정하고 해당 sink만 one-shot close한다. 호환성을
  위해 wire close reason 값 `6 QueuePressure`를 유지하지만 의미는 단순
  high-water가 아니라 **실제 admission loss**다.
- positive socket progress가 5초 연속 없으면 `TransmitNoProgress`로 해당
  sink만 close한다. vendor call 자체가 5초 이상 반환하지 않으면 facade는
  논리적으로 그 epoch를 격리하지만 같은 MCU의 kernel/WHD hard stall을 취소할
  수 있다고 주장하지 않는다.
- close 경로는 이전 epoch의 FIFO와 partial frame을 전부 flush한다.
- flush된 record와 close 중 거부된 record는 누계와 정확한 sequence loss
  경계로 남기며 정상 전송으로 위장하지 않는다.
- 다음 client가 연결되면 현재 `boot_session_id`, 현재 full
  `publish_seq64`, 새 connection epoch를 가진 fresh `STREAM_SESSION`을
  가장 먼저 보내고 그 뒤의 현재 Live만 전송한다.
- sink epoch가 요구한 sink mask는 delivery filter가 아니라 required
  acceptance mask다. anchor는 연결된 sink 전체에 동일 canonical bytes로
  한 번 fanout하고, 요구된 Wi-Fi sink가 이를 수락하지 못하면 main이
  해당 Wi-Fi epoch만 즉시 isolation/flush한다. USB 수락은 Wi-Fi 수락
  성공으로 대체되지 않는다.

무손실 장기 기록이 제품 요구가 되면 CSM RAM queue를 키우지 않는다. 별도
logger/storage에 용량, wear, retention과 export 계약을 승인해야 한다.

## identity와 손실 의미

- CAN truth: bus별 `capture_seq64`
- CAN segment: `segment_seq64`
- canonical truth: `boot_session_id + publish_seq64`
- socket delivery: connection epoch + socket sent sequence
- Wi-Fi loss: first/last dropped publish sequence + cumulative drop/flush
- Android Capture: 앱이 독립 소유하는 generation + file sequence

`STREAM_SESSION`은 현재 연결 구간의 기준점이지 replay 허가서가 아니다.
재연결은 이전 구간을 닫고 새 sink epoch와 새 consumer segment를 시작한다.
수신자는 sequence gap을 명시하되 최신 Live 처리를 계속한다.

## downlink와 wire compatibility

- record `21 APP_RX_COMMIT_ACK`는 legacy reserved다.
- current CSM은 legacy record 21을 frame 수준에서 decode-ignore하며 RAM
  회수, permission, disconnect 또는 다른 상태 변경에 사용하지 않는다.
- record `22 LINK_RELIABILITY_DIAGNOSTIC` schema 1은 기존 capture와 도구를
  위한 decode-only 계약이다. current product publication에는 사용하지 않는다.
- 현재 sink 진단은 `TRANSPORT_DIAGNOSTIC`, `BOARD_HEALTH`, `BOARD_EVENT`의
  queue/drop/epoch/close evidence로 판정한다.
- record ID 21 또는 schema 1 record 22를 다른 의미로 재사용하지 않는다.

## worker와 socket 경계

- `WifiSocketWorker` 하나만 AP, listener, accepted socket, send, receive,
  abort와 close를 소유한다.
- producer transition, critical/control request와 `sigio`가 worker를 깨우며
  bounded fallback이 notification loss를 제한한다.
- 한 pump는 설정된 write 수/byte/time budget을 넘지 않는다.
- receive 경로는 current allowlist의 typed downlink만 bounded mailbox로
  전달한다. legacy record 21은 무효 명령이나 transport fault로 승격하지 않고
  decode-ignore한다.
- RX ring reset은 worker 한 곳만 소유한다. close에서 odd generation으로
  invalidate하고, accept에서 이전 byte/parser 상태를 비운 뒤 새 even
  generation을 연다. facade가 관측한 generation과 mailbox generation이
  같을 때만 downlink를 노출한다.
- 각 vendor send 직전과 positive return을 회계한 직후 pressure/isolation
  request를 다시 확인한다. 한 epoch의 close 요청 뒤 같은 pump가 추가 send를
  호출하지 않는다.
- vendor call stall은 Wi-Fi epoch close를 요청하지만 같은 MCU의
  kernel/driver/SDIO hard stall을 물리적으로 격리한다고 주장하지 않는다.

## Mbed/lwIP와 메모리 경계

2026-07-28 pinned Mbed/lwIP/D3 profile은 내부 Wi-Fi candidate다. retained
journal의 정당화 근거였던 1.02초 replay 목표는 폐기됐다. pinned archive,
custom linker, D3 heap, MPU와 WHD override는 live FIFO 구현 후 실제 필요성과
RAM 비용을 다시 검증한다.

기존 candidate identity는 과거 artifact 재현에만 사용한다.

- ArduinoCore-mbed: `6816d442fd00bc17f83c73396d3d8d90285a6a8a`
- Mbed OS: `17dc3dc2e6e2817a8bd3df62f38583319f0e4fed`
- `libmbed.a` SHA-256:
  `032494298FC6CAFAAD23277B8CBEB01F1BA75CA7F72CCD90383A850EE561FD70`

새 product artifact는 exact source/archive/link map/MPU read-back과 실기
처리량을 함께 기록해야 한다. build 성공이나 PC synthetic peer만으로 내부
Wi-Fi를 제품 transport로 승인하지 않는다.

## 제품 RC/CAN 경계

- 제품 profile은 RP2040 feeder가 CAN0 SPI/ACK를 소유하고, J4 FDCAN은
  CAN1 관측과 승인된 RC vehicle command TX를 소유한다.
- `BOARD_ENABLE_PRODUCT_VEHICLE_COMMAND_MAPPING=1`은 feeder 제품 profile
  한 곳에서만 허용된다.
- legacy MCP profile, host raw CAN TX, Service/HIL joystick path와 MDPS
  bench mapping이 동시에 켜지면 compile-time 실패한다.
- 무장/중립/failsafe/authority와 실제 FDCAN completion evidence 계약은
  Wi-Fi 상태와 독립적으로 유지한다.

## qualification gate

구현 후 다음을 순서대로 통과해야 한다.

1. host contract: APP ACK/replay/rewind/retained anchor 부재, Wi-Fi FIFO
   `256/49152`, USB FIFO `192/40960`, transient 계산과 high/low hysteresis 확인
2. build/link: RAM `211,448/523,624 B`, D1 heap span `311,816 B`, Wi-Fi DTCM
   `53,248 B`, DTCM 잔여 `77,152 B`, 강제 reserve `65,536 B` 확인
3. startup fault injection: Wi-Fi 실패 중 RC/CAN/USB 정상
4. idle PC와 실제 Android Live: positive send 즉시 release와 backlog replay 0
5. 실제 reserve/full 및 5초 no-progress 강제: close, flush, loss evidence, fresh
   `STREAM_SESSION`, 최신 Live 복구
6. CAN0 2,000 fps + CAN1 2,000 fps + USB + PC Wi-Fi 소비자 동시 HIL — PASS,
   양 source sequence exact, 양 sink gap/drop/close 0
7. 같은 부하의 Android Wi-Fi + Capture 동시 HIL
8. Android Capture open/write/fsync 실패 중 Live/TCP 지속
9. reconnect 반복과 1시간/8시간/24시간 soak

현재 내부 Wi-Fi의 판정은 `IMPLEMENTED CANDIDATE / RELEASE BLOCKED`다.
