# BRIEF

Updated: 2026-07-23

## 2026-07-23 nonblocking Wi-Fi fanout closure

- The producer-side yield retry was discarded. Canonical main/RC/CAN enqueue is
  a nonblocking SPSC path; only the socket worker owns send/receive/close/abort.
- Bench evidence isolated the reconnect cause as `WifiCloseReason=6`: the old
  252-descriptor queue reached its 75% descriptor threshold while the 48 KiB
  byte pool was only about 36% occupied. It was not a board reset, watchdog,
  socket error, Android ingress overflow, or CAN/RC stall.
- Product Wi-Fi profiles now use 512 descriptors and a 48 KiB byte pool. Both
  high-water dimensions represent about 2.5 s for the measured ~95 B/record
  mix; descriptor counters are `uint16_t` and have a >255 contract test.
- `BOARD_EVENT 45` retains the authoritative close reason and disconnect count.
  The main loop merges both sink-service polls so event pulses are not lost.
- Final physical H7 + `SM-S936N` + USB observation held one TCP connection for
  95 s: disconnect/stall/socket/overflow/pressure-close all zero, queue byte
  high-water 22,759/49,152, and main-loop max gap 5,069 us.

## 2026-07-23 RC/Service shared vehicle bench contract

- RC source is CRSF CH2(index 1), positive forward. RC and Android Service/HIL now share standard `0x005` DLC8 drive (`AA 52 speedLE direction 00 00 00`, stop `AA 02 00 00 00 00 00 00`) and standard `0x007` DLC8 steering byte0 `10..130..250`/zero tail.
- Drive maps the post-deadband joystick linearly to `0..1000`, applies the existing time-equivalent slew and zero-before-reverse rule, and transmits at 100 Hz. Steering remains 50 Hz. Unqualified/lost/failsafe RC emits only the drive stop frame when autonomy is explicitly released and hard/hardware gates are healthy.
- Service/HIL permits RC or host through the same authority boundary, never both as motion owners. Host allowlist validates exact ID/DLC/payload. Normal app joystick release keeps ARM while sending neutral; lifecycle/session/authority failures still disarm.
- Wi-Fi identity uses a connection-edge `STREAM_SESSION` anchor, with an
  idempotent `HOST_QUERY_CAPABILITY` recovery handshake ordered as
  `STREAM_SESSION -> CAPABILITY -> CONTROL_ACK`; there is no periodic session
  record. Service/HIL CAN RX segmentation is 20 ms instead of 1 ms.
  Remote-control and uplink host contracts pass; live upload/reconnect
  verification, 3-minute soak, and external CAN cadence remain open.

## 2026-07-22 accepted socket 수명 결함 판정

- 실제 reset `ref.bin`과 현재 symbol build는 SHA-256 `06AA81E4AD4E696C40610E1F9D0322667729235008DD10EE2180727D592C9866`으로 동일하다. 이 binary는 Mbed factory-accepted `TCPSocket`에 `close()`를 호출한 뒤 deleting destructor를 다시 호출한다. Mbed 계약상 `close()`가 이미 객체를 해제하므로 확정적인 double-destruction/undefined behavior 결함이다.
- 실보드 retained evidence도 `CloseClient` 반환 직후 `DeleteClient` 진입, 미완료 상태에서 reset된 것을 보존했다. 따라서 이 결함은 관측 reset의 고신뢰 촉발 원인이다. raw reset latch가 0이므로 HardFault/watchdog 중 최종 reset executor는 아직 확정하지 않는다.
- accepted client 종료를 close-only로 수정하고 위험한 `delete owned`를 요구하던 architecture guard를 반대로 금지하도록 교정했다. phase 9/12는 과거 evidence 해석용 reserved 값으로 유지한다.
- 이전 REF/A/B/C 결과는 모두 `wifi_connect +0`, `wifi_sent +0`이므로 `USB_ONLY_IDLE / SYMPTOM_NOT_OBSERVED`로 강등한다. connected/reconnect 안정성 근거가 아니다.
- close-only REF는 실제 SM-S936N observer에서 성공한 reconnect 20회(정상 중지 1회, process stop 19회)를 포함한 600초 동안 boot sequence 6과 단일 session을 유지했다. CSM counter는 connect/disconnect `+22/+22`, Wi-Fi sent `+857`이며 CRC/gap/USB disconnect/quarantine/runtime contract error는 모두 0이다. 이 재현 결함은 `FIXED`로 판정하며 동시 HIL과 soak는 아직 남아 있다.
- I2 20초 preflight는 RC valid frame `+1987`, Wi-Fi sent `+320`, USB sent `+310`, 동일 boot/session, CRC/gap 0을 증명했지만 Kvaser physical channel 0은 ACK 0이고 CSM bus0/bus1 RX도 0이었다. 따라서 동시부하는 CAN 물리 경계의 `INVALID_LOAD`이며 reset 재발로 판정하지 않는다.

## 2026-07-22 crash-first self-debug 현재 상태

- `RuntimeSupervisor`와 `BootRecovery`가 risky driver보다 먼저 시작한다. backup
  SRAM `512..2815`에 교대 metadata 2슬롯과 compact event 64개 ring을
  checksum-last로 commit하고, boot sequence/last progress/30초 stable/early-reset/
  Wi-Fi quarantine을 `BOARD_HEALTH v13`에 투영한다. `3072..3199`의
  `RetainedCallLatch`는 Wi-Fi vendor call 진입 전과 반환 직후를 별도 보존한다.
- 같은 source/experiment selector에서 30초 이전 종료가 연속 2회 복구되면 다음 boot의 Wi-Fi를
  Off로 낮춘다. build timestamp만 바뀌어도 이력을 지우지 않으며 REF/A/B/C
  selector 변경은 새 구성에 한 번의 clean trial을 준다.
- reset matrix는 REF=`watchdog On+Full`, A=`watchdog On+Wi-Fi Off`,
  B=`watchdog Off+Full`, C=`watchdog On+AP-only`다. 네 artifact 모두 source hash
  `77bf1341a672984d8eb3d770234e3d2f7acf0957036a8eb705deadd997517856`로
  build됐고 application-data CAN TX는 compile-time 차단됐다.
- 2026-07-22 Portenta COM7의 REF/A/B/C 180초 결과는 단일 boot session, CRC 0,
  sequence gap 0이었지만 네 결과 모두 `wifi_connect +0`, `wifi_sent +0`이었다.
  따라서 `USB_ONLY_IDLE / SYMPTOM_NOT_OBSERVED`이며 Wi-Fi reset matrix 합격이 아니다.
- reset raw는 여전히 0/unknown이었다. Arduino bootloader가 application보다 먼저
  RCC latch를 지울 수 있으므로 bootloader early capture 또는 외부 power/reset
  evidence 전에는 watchdog/power/hard fault를 확정하지 않는다.
- 선택된 PlatformIO env와 material flag는 deterministic runtime contract ID로
  source ID/selector와 합성되어 recovery identity를 이룬다.

## 2026-07-21 Wi-Fi 실행 경계 리팩터링

- `WifiTcpSink`에서 모든 AP/socket 호출과 socket lifetime을 제거하고,
  단일 `WifiSocketWorker`가 configure/AP/server/accept/send/recv/close와 socket
  lifetime을 소유하도록 분리했다. accepted factory socket은 close-only다.
- main/publisher 경계는 bounded TX/RX mailbox의 try-offer와 cached snapshot만
  사용한다. 250 ms 이상 진행 중인 호출은 phase/sequence/heartbeat로 관측한다.
  같은 M7/kernel/WHD/radio/전원을 공유하므로 vendor stall과 power fault까지
  물리 격리됐다고 주장하지 않는다.
- Wi-Fi runtime은 Disabled/AP-only/Full TCP로 분리하고 startup attempt를 기본
  1회로 제한했다. 이 제한은 한 번의 `beginAP()` 반환 시간을 보장하지 않는다.
- runtime diagnostic schema 2는 Wi-Fi call phase/sequence/start/duration/result,
  worker heartbeat, stall count, epoch를 포함한다. retained evidence는 Portenta
  bootloader heap과 겹치던 일반 RAM 대신 STM32H747 backup SRAM을 사용한다.
- boot recovery/runtime supervisor/Wi-Fi isolation/remote control/BOARD_HEALTH v13/
  canonical fanout host contract와 architecture/source/profile guard가 통과했다.
  blocked-client/RC/USB 동시 HIL은 다음 gate다.

## 2026-07-20 실장비 결과

- Arduino `WiFiClient`의 내부 RX thread/동기 종료 경로를 제거하고, 단일 owner의 nonblocking raw Mbed `TCPSocket`과 고정 downlink buffer로 Wi-Fi sink를 구현했다. `BOARD_HEALTH v11`은 socket timing과 이전 runtime breadcrumb를 제공한다.
- 실제 Portenta H7 Service/HIL, `SM-S936N` Android, USB, Kvaser CAN1 20 Hz 동시 시험에서 12초 preflight와 180초 창이 통과했다. 180초 동안 CAN `+3580`, USB `+3776`, Wi-Fi `+3776`, CRC/gap/drop/overflow/MCP/socket/stall delta는 모두 0이었다.
- 이어진 300초 창은 동일 boot session, CAN truth 무손실, Wi-Fi 오류 0을 유지했지만 MCP2515 SPI `+3`, error flag `+1` 때문에 strict gate는 실패했다. event context는 MCP status read의 `CANINTF=0xFF`였고 FIFO overflow, CAN drop, sequence gap은 0이었다.
- 이후 무인 운용 중 red LED와 Android 재연결이 발생했고 boot session이 `0x20E6A7BEEE396122`에서 `0xB24FC5C68F20DA5B`로 바뀌어 CSM 재부팅으로 판정했다. 새 boot의 Wi-Fi disconnect/stall/socket delta는 0이고 retained breadcrumb가 남지 않아 hard reset 또는 power-path interruption이 open risk다.

## 저장소 기준선

- 원격 저장소: `https://github.com/JO-DOHYUN/csm_remote.git`
- 분기 기준 commit: `d2a1f87fae3e44ba978f9f483dd4b8799cee32ad`
- 작업 branch: `codex/vsm-wifi-fanout`
- 정식 PlatformIO project: `firmware/csm`

## 확인된 현재 사실

- 2026-07-22 PCAN/J4에서 RC/MDPS 벤치 매핑을 실측했다. 30초간 ID `0x007` 1,452개, 주기 중앙값 20.693 ms/최대 21.761 ms였고, CH5 독점 byte0..6 `0x00` + byte7 `0x01/0x80`, CH10/CH11 조향 유지 + byte7 오버레이를 모두 확인했다. 이는 실제 송신 payload/주기 증거이며 별도 송신원 대비 CAN 무손실 증명은 아니다.

- passive M7 env `portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive`가 clean clone에서 build된다.
- 제품 M4 env `portenta_h7_m4_remote_frontend`는 Serial3 CRSF 416666 8N1, 16채널/링크 통계, 정규화, 양방향 telemetry와 SRAM4 IPC를 포함해 build된다.
- 제품 M7 env `portenta_h7_m7_mid_mcp2515_j4_remote_product_wifi`는 RC authority/limiter 관측 경계, built-in CAN bus 1 RX, canonical USB/Wi-Fi evidence를 포함한다. mapper는 `None`, local CAN TX capability는 Off다.
- vehicle 실차 전 벤치 env `portenta_h7_m7_mid_mcp2515_j4_remote_product_mdps_bench_wifi`는 같은 제품 authority/safety/fanout을 유지하고 `VehicleBench0x005And0x007`의 J4 송신만 연다. MCP2515는 normal-mode RX/ACK만 허용하며 host/control TX는 계속 금지한다.
- 2026-07-22 REF 15초 실측에서 J4 `+300`, RC valid/accepted `+1487`, boot session 변화와 CAN/USB drop은 0이었다. 당시 RC 출력 부재는 수신 고장이 아니라 REF의 `BOARD_DIAG_SUPPRESS_REMOTE_CAN_TX=1` 때문이었다.
- remote product architecture, Phase 2A, Phase 2B guard와 전체 runtime 계약시험이 통과한다.
- hard safety 뒤 upstream autonomy가 `InactiveConfirmed`로 release해야 RC reservation을 평가한다. 그 뒤 500 ms 중립 qualification, 정상 stale/failsafe의 즉시 중립과 1초 release를 적용하며 malformed/protocol/IPC/M4 failure는 release하지 않는다. autonomy runtime wiring은 아직 없다.
- SRAM4 schema 2 IPC는 header, M4→M7, M7→M4를 32-byte cache-line 단독 소유 영역으로 분리한다. M4/M7 artifact는 항상 한 쌍으로 배포한다.
- typed v1 frame은 유지하면서 `CanonicalPublisher`가 fanout 전에 `publish_seq64`를 배정하고 `seq u16`에 하위 16비트를 기록한다. `STREAM_SESSION`이 full identity를 고정한다.
- `TypedRecords.h`가 CAN raw/segment와 `BOARD_HEALTH v13` field offset constants를
  제공하며 v13은 v12의 472-byte prefix를 그대로 보존한다.
- `RecordAdmission`, one-encode `CanonicalPublisher`, fixed `UsbCdcSink`, fixed `WifiTcpSink` dual fanout이 구현되어 있다. Wi-Fi sink는 48-record queue, 4-record critical reserve, 2-record/75 ms bounded batching을 사용한다.
- Wi-Fi observer env는 Arduino Mbed Wi-Fi AP direct와 TCP server `192.168.4.1:3333`, client 1개를 사용한다.
- host fanout contract test, passive M7 build, Wi-Fi observer M7 build, M4 frontend proof/probe와 passive symbol guard가 통과했다.
- 2026-07-15 D-016 Android diagnostics binding 변경 후 host fanout contract와 passive/Wi-Fi observer M7 build를 다시 통과했다.
- 2026-07-16 현재 Wi-Fi observer M7 회귀 build 사용량은 RAM 74.6%/Flash 46.3%다.
- 2026-07-15 COM7 1200-bps touch로 DFU 전환 후 Wi-Fi observer firmware 실제 업로드가 성공했다. 수동 double-reset은 필요하지 않았다.
- 실제 보드 AP `VSM-CSM-DEV`, Android TCP client, `STREAM_SESSION`, `BOARD_HEALTH` 수신이 확인됐다.
- connection poll을 session gate보다 먼저 수행하도록 수정했고 Wi-Fi stalled-client close 기준을 5 s로 조정했다. 최종 60 s status-stream에서 epoch 1, connect 1, disconnect/stall/overflow 0이었다.
- 2026-07-16 Service/HIL 실측에서 비활성 encoder가 0값 `ENC_DERIVED`를 계속 발행하고 disabled timer를 fault로 광고하던 계약을 제거했다. 해당 profile은 encoder capability도 0으로 광고한다.
- Kvaser CAN1 `0x50` 20 Hz와 CSM Wi-Fi를 같은 30 s 창에서 계측한 최종 artifact는 `C:\WORKS\VS\vsm_android_app\build\hil\20260716-114703-kvaser-can1-wifi-observer`다. CSM health-window source/bus0는 `580/580`, CAN/FIFO/Wi-Fi drop과 typed/segment/capture gap은 모두 0이었다.
- 2026-07-16의 폐기 전 `0x100/0x200` 계약에서는 PC HIL client로 canonical heartbeat/ARM/neutral/disarm과 matching `CAN_TX_RAW`를 확인했다. 이 기록은 D-016의 현행 `0x005/0x007` 검증으로 승계되지 않는다.
- 2026-07-20 당시 dirty build 사용량은 M4 RAM 14.7%/Flash 7.0%, M7 RAM 74.7%/Flash 44.0%였다. 현재 reset REF build는 RAM 79.5%/Flash 45.5%다.
- 실제 R16SM CRSF/telemetry, M4-M7 IPC, autonomy wiring, 실제 vehicle mapping, D1 hardware gate, completion-correlated CAN TX, production Wi-Fi 동시 운용, dual-CAN 고부하와 장시간 reset gate는 아직 검증되지 않았다.

## 확정된 목표

- M4 RC frontend, M7 단일 authority/CAN TX owner를 유지한다.
- M7 evidence를 canonical publisher에서 한 번 직렬화한다.
- USB CDC와 Wi-Fi TCP는 bounded 독립 sink로 fanout한다.
- Wi-Fi 1차 제품은 Android observer 한 대, live-only, reconnect 시 새 epoch, board backlog replay 없음이다.
- Windows USB observer와 Android Wi-Fi observer는 RC 운용 중 동시에 사용할 수 있어야 한다.

## 다음 구현 gate

1. close-only 수정 build의 실제 Android connect/stream/disconnect 20회와 graceful/abrupt reconnect 검증은 완료했다.
2. 같은 revision으로 Windows USB + Android Wi-Fi + CAN + RC 동시 300초와 장시간 soak를 수행한다.
3. 재발 시 bootloader reset latch 또는 외부 power/reset evidence를 구현·대조한다.
4. R16SM CRSF/telemetry와 M4-M7 IPC를 실제 장비에서 검증한다.
5. upstream autonomy runtime profile, 실제 차량 mapping, D1 hardware gate를 승인한다.
6. FDCAN TX completion/TXBTO와 상관된 `CAN_TX_RAW`를 구현하고 Kvaser에서
   ID/payload/주기/ACK를 대조한다. `0x007` mapper는 bench 전용이다.
7. Windows USB + Android Wi-Fi + RC + dual CAN 동시 HIL, fault injection,
   blocked-client/reconnect와 장시간 soak를 수행한다.

Android 전용 wire 형식이나 sink별 별도 encode 경로는 만들지 않는다.
