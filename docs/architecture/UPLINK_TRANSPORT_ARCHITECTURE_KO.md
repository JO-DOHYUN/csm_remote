# CSM Uplink Transport 아키텍처

Updated: 2026-07-28

이 문서는 현재 CSM 제품 데이터 경계의 권위 문서다. 과거 queue/재연결
실험은 `history/decisions/DECISION_LEDGER_KO.md`에만 남긴다.

## 최종 데이터 흐름

```text
M4 CRSF latest sample --------\
RP2040 feeder UART CAN0 -------+--> M7 deterministic executive
J4 FDCAN CAN1 ----------------/          |
                                      bounded RecordAdmission
                                              |
                                      CanonicalPublisher
                                  (identity 1회, encode 1회)
                                     /                  \
                          UsbCdcSink queue       Wi-Fi retained journal
                                                       |
                                             one RTOS socket worker
                                                       |
                                            WHD SoftAP + raw TCPSocket
                                                       |
                                       Android ordered SessionCore
                                                       |
                                          bounded capture writer
                                                       |
                                             fsync durable commit
                                                       |
                                    APP_RX_COMMIT_ACK (record 21)
```

- RC, authority, safety, CAN ingest와 canonical publisher는 network API를
  호출하거나 기다리지 않는다.
- USB와 Wi-Fi는 독립 queue, cursor, epoch와 failure evidence를 소유한다.
- canonical bytes와 `publish_seq64`는 sink 앞에서 한 번만 생성된다.
- M4/Feeder는 입력 frontend이고 제품 권한·안전·CAN TX ownership은 M7에
  남는다.

## Wi-Fi 보존 계약

Wi-Fi 저장소는 `ReliableFrameJournal<1024, 65520>`이다.

- descriptor 1,024개 × 24 B + encoded byte ring 65,520 B =
  DTCM 90,096 B이다.
- normal admission은 마지막 4 descriptor/2,112 B를 critical evidence에
  예약한다.
- producer는 release-store로 완성 descriptor만 공개하고 절대 retry,
  yield, socket wait를 하지 않는다.
- positive socket return은 send cursor만 전진시킨다.
- Android가 원본 typed frame을 ordered capture에 기록하고 `fsync`한 뒤
  보낸 누적 ACK만 reclaim cursor를 전진시킨다.
- socket close/reconnect는 reclaim cursor까지 rewind한다. ACK되지 않은
  record는 같은 boot session에서 재전송된다.
- ACK boot ID 불일치, 아직 전송하지 않은 sequence, 비연속 sequence는
  모두 거절되고 counter로 남는다.

이 경계는 무한 보존을 뜻하지 않는다. 1,024 descriptor/65,520 B를 넘는
장기 단절은 다음처럼 fail-visible 처리한다.

1. 최초 Reserved/Full에서 `first_not_admitted_publish_seq`를 고정한다.
2. integrity fault를 latch하고 worker에 one-shot epoch close를 요청한다.
3. `BOARD_EVENT 52`와 record 22 진단에 손실 경계를 남긴다.
4. 기존 journal을 덮어쓰지 않는다.
5. Android는 sequence/parser/capture/storage failure에서 ACK를 fence하고
   해당 epoch를 닫는다.

완전한 장기 무손실이 제품 요구가 되면 RAM 확대가 아니라 별도
flash journal 용량·wear·retention 계약을 승인해야 한다.

## 연결 identity

- CAN truth: bus별 `capture_seq64`
- CAN segment: `segment_seq64`
- canonical truth: `boot_session_id + publish_seq64`
- socket delivery: connection epoch + send/reclaim cursor
- Android durability: capture generation + committed publish sequence

`STREAM_SESSION`은 boot ID와 full publish sequence를 고정한다. 새 client가
수락되면 main은 다른 canonical record를 publish하기 전에 session
announcement를 우선 처리한다. 재연결 때 journal 앞에 남아 있는 record는
이미 확인된 동일 boot identity로만 복구할 수 있다. identity를 증명할 수
없는 fresh consumer는 임의 ACK하지 않고 fail closed한다.

## worker와 socket 경계

- `WifiSocketWorker` 하나만 AP, listener, accepted socket, send, receive,
  abort, close를 소유한다.
- producer transition, critical record, control request와 `sigio`가 worker를
  깨우며 10 ms fallback이 notification loss를 제한한다.
- 한 pump는 설정된 write 수/byte/time budget을 넘지 않는다.
- partial write는 record 경계를 유지한 채 남은 byte부터 재개한다.
- receive 경로는 record 21을 worker에서 소비하고 나머지 typed downlink만
  bounded passthrough mailbox로 전달한다.
- vendor call stall은 sink를 논리적으로 격리할 수 있지만 같은 MCU의
  kernel/driver/SDIO hard stall을 물리적으로 격리한다고 주장하지 않는다.

## pinned Mbed/lwIP 메모리 경계

제품 환경
`portenta_h7_m7_mid_feeder_uart_j4_remote_product_wifi`만 pinned archive와
전용 linker를 사용한다.

- ArduinoCore-mbed: `6816d442fd00bc17f83c73396d3d8d90285a6a8a`
- Mbed OS: `17dc3dc2e6e2817a8bd3df62f38583319f0e4fed`
- `libmbed.a` SHA-256:
  `032494298FC6CAFAAD23277B8CBEB01F1BA75CA7F72CCD90383A850EE561FD70`
- TCP MSS 1,460; send buffer 11,680 B; receive window 5,840 B;
  40 TCP segments; lwIP heap 40,960 B; TCP/IP stack 4,096 B;
  sockets 최대 4; IPv4 only; WHD TX `PBUF_RAM`.
- lwIP heap object 40,979 B는 D3
  `0x38000400..0x3800A7FF`의 41,984 B envelope에 link-time 고정된다.
- setup 첫 단계에서 MPU region 15를 D3 64 KiB
  non-cacheable/shareable/XN으로 설치하고 read-back한다. 실패하면 Wi-Fi를
  시작하지 않고 USB와 `BOARD_EVENT 52`를 살린다.
- Ethernet DMA/lwIP D2 section은 M4 소유 경계 뒤의
  `0x30040000..0x30048000` 안에 남는다.
- DTCM journal은 90,096/130,408 B이며 linker가 최소 32 KiB reserve를
  강제한다.

`tools/build_pinned_mbed.ps1`은 source commit, generated config, 교체 object
목록과 출력 SHA를 manifest에 고정하고 deterministic archive를 만든다.
PlatformIO pre-script는 manifest hash 불일치 시 제품 빌드를 중단한다.

## 제품 RC/CAN 경계

- 제품 profile은 RP2040 feeder가 CAN0 SPI/ACK를 소유하고, J4 FDCAN은
  CAN1 관측과 승인된 RC vehicle command TX를 소유한다.
- `BOARD_ENABLE_PRODUCT_VEHICLE_COMMAND_MAPPING=1`은 feeder 제품 profile
  한 곳에서만 허용된다.
- legacy MCP profile, host raw CAN TX, Service/HIL joystick path와 MDPS
  bench mapping이 동시에 켜지면 compile-time 실패한다.
- 무장/중립/failsafe/authority와 실제 FDCAN completion evidence 계약은
  기존 deterministic executive를 그대로 따른다.

## 현재 판정과 다음 gate

오프라인 통과:

- product M7, legacy MCP M7, M4 remote frontend, RP2040 feeder build
- native contract 11종
- Wi-Fi architecture, RC product, control execution guard
- 계산상 2,000 fps 57,735 B/s와 1.02 s journal retention

실기 미실행:

1. M4/M7/Feeder paired upload와 boot/session identity 확인
2. AP idle 및 PC/Android durable ACK/replay
3. 2,000 fps + J4 RC + USB + Wi-Fi 동시 무결성
4. blocked client, reconnect, app kill/restart, storage-failure fault injection
5. 장시간 soak와 실제 Android 기기 사용자 시나리오

이 다섯 행을 통과하기 전에는 내부 Wi-Fi를 release-qualified로 표시하지
않는다.
