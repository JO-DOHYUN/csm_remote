# BRIEF

Updated: 2026-07-20

## 2026-07-20 실장비 결과

- Arduino `WiFiClient`의 내부 RX thread/동기 종료 경로를 제거하고, 단일 owner의 nonblocking raw Mbed `TCPSocket`과 고정 downlink buffer로 Wi-Fi sink를 구현했다. `BOARD_HEALTH v11`은 socket timing과 이전 runtime breadcrumb를 제공한다.
- 실제 Portenta H7 Service/HIL, `SM-S936N` Android, USB, Kvaser CAN1 20 Hz 동시 시험에서 12초 preflight와 180초 창이 통과했다. 180초 동안 CAN `+3580`, USB `+3776`, Wi-Fi `+3776`, CRC/gap/drop/overflow/MCP/socket/stall delta는 모두 0이었다.
- 이어진 300초 창은 동일 boot session, CAN truth 무손실, Wi-Fi 오류 0을 유지했지만 MCP2515 SPI `+3`, error flag `+1` 때문에 strict gate는 실패했다. event context는 MCP status read의 `CANINTF=0xFF`였고 FIFO overflow, CAN drop, sequence gap은 0이었다.
- 이후 무인 운용 중 red LED와 Android 재연결이 발생했고 boot session이 `0x20E6A7BEEE396122`에서 `0xB24FC5C68F20DA5B`로 바뀌어 CSM 재부팅으로 판정했다. 새 boot의 Wi-Fi disconnect/stall/socket delta는 0이고 retained breadcrumb가 남지 않아 hard reset 또는 power-path interruption이 open risk다.

## 저장소 기준선

- 원격 저장소: `https://github.com/JO-DOHYUN/csm_remote.git`
- 분기 기준 commit: `51b411191aa7410df9b2bfecbddd040451287db0`
- 작업 branch: `codex/vsm-wifi-fanout`
- 정식 PlatformIO project: `firmware/csm`

## 확인된 현재 사실

- passive M7 env `portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive`가 clean clone에서 build된다.
- M4 frontend proof와 M4 Serial3 capture probe env가 build된다.
- Phase 1, 2A, 2B guard가 통과한다.
- remote/authority/control 코드는 deny-first skeleton이며 production runtime에 연결되지 않았다.
- typed v1 frame은 유지하면서 `CanonicalPublisher`가 fanout 전에 `publish_seq64`를 배정하고 `seq u16`에 하위 16비트를 기록한다. `STREAM_SESSION`이 full identity를 고정한다.
- Android generated binding이 handwritten offset을 만들지 않도록 `TypedRecords.h`가 CAN raw/segment와 BOARD_HEALTH v8 field offset constants를 제공한다. wire byte layout은 변경되지 않았다.
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
- 동일 bench에서 Android와 같은 canonical heartbeat/ARM/neutral/disarm을 PC HIL client로 실행했다. artifact `C:\WORKS\VS\vsm_android_app\build\hil\20260716-115511-service-neutral-control`에서 모든 ACK accepted, matching `CAN_TX_RAW`, Kvaser `0x100=0x32`와 `0x200=0x32`를 확인했다.
- 실제 R16SM, M4-M7 IPC, production Wi-Fi module, dual-CAN 고부하, blocked client, 동시 USB/Wi-Fi, RC timing/차량 HIL은 이 branch에서 검증되지 않았다.

## 확정된 목표

- M4 RC frontend, M7 단일 authority/CAN TX owner를 유지한다.
- M7 evidence를 canonical publisher에서 한 번 직렬화한다.
- USB CDC와 Wi-Fi TCP는 bounded 독립 sink로 fanout한다.
- Wi-Fi 1차 제품은 Android observer 한 대, live-only, reconnect 시 새 epoch, board backlog replay 없음이다.
- Windows USB observer와 Android Wi-Fi observer는 RC 운용 중 동시에 사용할 수 있어야 한다.

## 다음 구현 gate

1. Android Service/HIL ARM, neutral request, ACK와 matching `CAN_TX_RAW`를 Kvaser로 대조한다.
2. 제어된 dual-CAN 고부하 source로 CAN evidence와 source count를 대조한다.
3. reconnect와 blocked-client fault injection을 수행한다.
4. 기존 USB Windows VSM, RC, dual CAN, Wi-Fi 동시 HIL과 장시간 soak를 수행한다.

Android 전용 wire 형식이나 sink별 별도 encode 경로는 만들지 않는다.
